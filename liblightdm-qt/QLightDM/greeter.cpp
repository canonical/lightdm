/*
 * Copyright (C) 2010-2011 David Edmundson
 * Copyright (C) 2010-2011 Robert Ancell
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 * 
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#include "greeter.h"

#include "user.h"
#include "sessionsmodel.h"
#include "config.h"

#include <security/pam_appl.h>

#include <QtNetwork/QHostInfo> //needed for localHostName
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QVariant>
#include <QtCore/QSettings>
#include <QtCore/QUrl>
#include <QtCore/QFile>
#include <QtCore/QSocketNotifier>
#include <QtDBus/QDBusPendingReply>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>

typedef enum
{
    /* Messages from the greeter to the server */
    GREETER_MESSAGE_CONNECT                 = 1,
    GREETER_MESSAGE_LOGIN                   = 2,
    GREETER_MESSAGE_LOGIN_AS_GUEST          = 3,
    GREETER_MESSAGE_CONTINUE_AUTHENTICATION = 4,
    GREETER_MESSAGE_START_SESSION           = 5,
    GREETER_MESSAGE_CANCEL_AUTHENTICATION   = 6,

    /* Messages from the server to the greeter */
    GREETER_MESSAGE_CONNECTED               = 101,
    GREETER_MESSAGE_QUIT                    = 102,
    GREETER_MESSAGE_PROMPT_AUTHENTICATION   = 103,
    GREETER_MESSAGE_END_AUTHENTICATION      = 104,
    GREETER_MESSAGE_USER_DEFAULTS           = 106
} GreeterMessage;

#define HEADER_SIZE 8

using namespace QLightDM;

class GreeterPrivate
{
public:
    QString theme;
    QString defaultSession;
    QString timedUser;
    int loginDelay;
    bool guestAccountSupported;

    SessionsModel *sessionsModel;
    Config *config;

    QDBusInterface* lightdmInterface;
    QDBusInterface* powerManagementInterface;
    QDBusInterface* consoleKitInterface;

    int toServerFd;
    int fromServerFd;
    QSocketNotifier *n;
    char *readBuffer;
    int nRead;
    bool inAuthentication;
    bool isAuthenticated;
    QString authenticationUser;
    int authenticateSequenceNumber;
};


Greeter::Greeter(QObject *parent) :
    QObject(parent),
    d(new GreeterPrivate)
{
    d->readBuffer = (char *)malloc (HEADER_SIZE);
    d->nRead = 0;
    d->config = 0;
    d->sessionsModel = new SessionsModel(this);
    d->authenticateSequenceNumber = 0;
}

Greeter::~Greeter()
{
    delete d->readBuffer;
    delete d;
}

static int intLength()
{
    return 4;
}

static int stringLength(QString value)
{
    QByteArray a = value.toUtf8();  
    return intLength() + a.size();
}

void Greeter::writeInt(int value)
{
    char buffer[4];
    buffer[0] = value >> 24;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;   
    if (write(d->toServerFd, buffer, intLength()) != intLength()) {
        qDebug() << "Error writing to server";
    }
}

void Greeter::writeString(QString value)
{
    QByteArray a = value.toUtf8();
    writeInt(a.size());
    if (write(d->toServerFd, a.data(), a.size()) != a.size()) {
        qDebug() << "Error writing to server";
    }
}

void Greeter::writeHeader(int id, int length)
{
    writeInt(id);
    writeInt(length);
}

void Greeter::flush()
{
    fsync(d->toServerFd);
}

int Greeter::getPacketLength()
{
    int offset = intLength();
    return readInt(&offset);
}

int Greeter::readInt(int *offset)
{
    if(d->nRead - *offset < intLength()) {
        qDebug() << "Not enough space for int, need " << intLength() << ", got " << (d->nRead - *offset);
        return 0;
    }

    char *buffer = d->readBuffer + *offset;
    int value = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
    *offset += intLength();
    return value;
}

QString Greeter::readString(int *offset)
{
    int length = readInt(offset);
    if(d->nRead - *offset < length) {
        qDebug() << "Not enough space for string, need " << length << ", got " << (d->nRead - *offset);
        return "";
    }
    char *start = d->readBuffer + *offset;
    *offset += length;
    return QString::fromUtf8(start, length);
}

void Greeter::connectToServer()
{
    QDBusConnection busType = QDBusConnection::systemBus();
    QString ldmBus(qgetenv("LDM_BUS"));
    if(ldmBus == QLatin1String("SESSION")) {
        busType = QDBusConnection::sessionBus();
    }

    d->lightdmInterface = new QDBusInterface("org.freedesktop.DisplayManager", "/org/freedesktop/DisplayManager", "org.freedesktop.DisplayManager", busType);
    d->powerManagementInterface = new QDBusInterface("org.freedesktop.PowerManagement","/org/freedesktop/PowerManagement", "org.freedesktop.PowerManagement");
    d->consoleKitInterface = new QDBusInterface("org.freedesktop.ConsoleKit", "/org/freedesktop/ConsoleKit/Manager", "org.freedesktop.ConsoleKit");

    QString file;
    file = d->lightdmInterface->property("ConfigFile").toString();
    qDebug() << "Loading configuration from " << file;
    d->config = new Config(file, this);

    char* fd = getenv("LDM_TO_SERVER_FD");
    if(!fd) {
       qDebug() << "No LDM_TO_SERVER_FD environment variable";
       return;
    }
    d->toServerFd = atoi(fd);


    qDebug() << "***connecting to server";
    QFile toServer;
    qDebug() << toServer.open(d->toServerFd, QIODevice::WriteOnly);

    fd = getenv("LDM_FROM_SERVER_FD");
    if(!fd) {
       qDebug() << "No LDM_FROM_SERVER_FD environment variable";
       return;
    }
    d->fromServerFd = atoi(fd);

    d->n = new QSocketNotifier(d->fromServerFd, QSocketNotifier::Read);
    connect(d->n, SIGNAL(activated(int)), this, SLOT(onRead(int)));

    qDebug() << "Connecting to display manager...";
    writeHeader(GREETER_MESSAGE_CONNECT, 0);
    flush();
}

void Greeter::login(const QString &username)
{
    d->inAuthentication = true;
    d->isAuthenticated = false;
    d->authenticationUser = username;
    qDebug() << "Starting authentication for user " << username << "...";
    writeHeader(GREETER_MESSAGE_LOGIN, intLength() + stringLength(username));
    d->authenticateSequenceNumber++;
    writeInt(d->authenticateSequenceNumber);
    writeString(username);
    flush();
}

void Greeter::loginAsGuest()
{
    d->authenticateSequenceNumber++;
    d->inAuthentication = true;
    d->isAuthenticated = false;
    d->authenticationUser = "";
    qDebug() << "Starting authentication for guest account";
    writeHeader(GREETER_MESSAGE_LOGIN_AS_GUEST, intLength());
    writeInt(d->authenticateSequenceNumber);
    flush();     
}

void Greeter::respond(const QString &response)
{
    qDebug() << "Providing response to display manager";
    writeHeader(GREETER_MESSAGE_CONTINUE_AUTHENTICATION, intLength() + stringLength(response));
    // FIXME: Could be multiple response required
    writeInt(1);
    writeString(response);
    flush();
}

void Greeter::cancelAuthentication()
{
    qDebug() << "Cancelling authentication";
    writeHeader(GREETER_MESSAGE_CANCEL_AUTHENTICATION, 0);
    flush();  
}

bool Greeter::inAuthentication() const
{
    return d->inAuthentication;
}

bool Greeter::isAuthenticated() const
{
    return d->isAuthenticated;
}

QString Greeter::authenticationUser() const
{
    return d->authenticationUser;
}

void Greeter::startSession(const QString &session)
{
    qDebug() << "Starting session " << session;
    writeHeader(GREETER_MESSAGE_START_SESSION, stringLength(session));
    writeString(session);
    flush();
}

void Greeter::onRead(int fd)
{
    //qDebug() << "Reading from server";

    int nToRead = HEADER_SIZE;
    if(d->nRead >= HEADER_SIZE)
        nToRead += getPacketLength();
  
    ssize_t nRead;
    nRead = read(fd, d->readBuffer + d->nRead, nToRead - d->nRead);
    if(nRead < 0)
    {
        qDebug() << "Error reading from server";
        return;
    }
    if (nRead == 0)
    {
        qDebug() << "EOF reading from server";
        return;
    }  

    //qDebug() << "Read " << nRead << "octets";
    d->nRead += nRead;
    if(d->nRead != nToRead)
        return;
  
    /* If have header, rerun for content */
    if(d->nRead == HEADER_SIZE)
    {
        nToRead = getPacketLength();
        if(nToRead > 0)
        {
            d->readBuffer = (char *)realloc(d->readBuffer, HEADER_SIZE + nToRead);
            onRead(fd);
            return;
        }
    }

    int offset = 0;
    int id = readInt(&offset);
    readInt(&offset);
    int nMessages, sequenceNumber, returnCode;
    switch(id)
    {
    case GREETER_MESSAGE_CONNECTED:
        d->theme = readString(&offset);
        d->defaultSession = readString(&offset);
        d->timedUser = readString(&offset);
        d->loginDelay = readInt(&offset);
        d->guestAccountSupported = readInt(&offset) != 0;
        qDebug() << "Connected theme=" << d->theme << " default-session=" << d->defaultSession << " timed-user=" << d->timedUser << " login-delay" << d->loginDelay << " guestAccountSupported" << d->guestAccountSupported;

        /* Set timeout for default login */
        if(d->timedUser != "" && d->loginDelay > 0)
        {
            qDebug() << "Logging in as " << d->timedUser << " in " << d->loginDelay << " seconds";
            //FIXME: d->login_timeout = g_timeout_add (d->login_delay * 1000, timed_login_cb, greeter);
        }
        emit connected();
        break;
    case GREETER_MESSAGE_QUIT:
        qDebug() << "Got quit request from server";
        emit quit();
        break;
    case GREETER_MESSAGE_PROMPT_AUTHENTICATION:
        nMessages = readInt(&offset);
        qDebug() << "Prompt user with " << nMessages << " message(s)";
        for(int i = 0; i < nMessages; i++)
        {
            int msg_style = readInt (&offset);
            QString msg = readString (&offset);

            // FIXME: Should stop on prompts?
            switch (msg_style)
            {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON:
                emit showPrompt(msg);
                break;
            case PAM_ERROR_MSG:
                emit showError(msg);
                break;
            case PAM_TEXT_INFO:
                emit showMessage(msg);
                break;
            }
        }
        break;
    case GREETER_MESSAGE_END_AUTHENTICATION:
        sequenceNumber = readInt(&offset);
        returnCode = readInt(&offset);

        if (sequenceNumber == d->authenticateSequenceNumber)
        {
            qDebug() << "Authentication complete with return code " << returnCode;
            d->isAuthenticated = (returnCode == 0);
            if(!d->isAuthenticated) {
                d->authenticationUser = "";
            }
            emit authenticationComplete(d->isAuthenticated);
            d->inAuthentication = false;
        }
        else
            qDebug () << "Ignoring end authentication with invalid sequence number " << sequenceNumber;
        break;
    default:
        qDebug() << "Unknown message from server: " << id;
    }

    d->nRead = 0;
}

QString Greeter::hostname() const
{
    return QHostInfo::localHostName();
}

QString Greeter::theme() const
{
    return d->theme;
}

QVariant Greeter::getProperty(const QString &name) const
{
    QUrl themeUrl(d->theme);
    QSettings themeInfo(themeUrl.path(), QSettings::IniFormat);
    return themeInfo.value(name);
}

QString Greeter::defaultLanguage() const
{
    return getenv("LANG");
}

QString Greeter::defaultSession() const
{
    return d->defaultSession;
}

bool Greeter::guestAccountSupported() const
{
    return d->guestAccountSupported;
}

QString Greeter::timedLoginUser() const
{
    return d->timedUser;
}

int Greeter::timedLoginDelay() const
{
    return d->loginDelay;
}

void Greeter::cancelTimedLogin() 
{
    //FIXME TODO
}

Config* Greeter::config() const
{
    return d->config;
}

bool Greeter::canSuspend() const
{
    QDBusReply<bool> reply = d->powerManagementInterface->call("CanSuspend");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void Greeter::suspend()
{
    d->powerManagementInterface->call("Suspend");
}

bool Greeter::canHibernate() const
{
    QDBusReply<bool> reply = d->powerManagementInterface->call("CanHibernate");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void Greeter::hibernate()
{
    d->powerManagementInterface->call("Hibernate");
}

bool Greeter::canShutdown() const
{
    QDBusReply<bool> reply = d->consoleKitInterface->call("CanStop");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void Greeter::shutdown()
{
    d->consoleKitInterface->call("stop");
}

bool Greeter::canRestart() const
{
    QDBusReply<bool> reply = d->consoleKitInterface->call("CanRestart");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void Greeter::restart()
{
    d->consoleKitInterface->call("Restart");
}
