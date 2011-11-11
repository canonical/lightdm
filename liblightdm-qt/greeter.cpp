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


#include "QLightDM/greeter.h"

#include "config.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QVariant>
#include <QtCore/QSettings>
#include <QtCore/QUrl>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QSocketNotifier>
#include <QtDBus/QDBusPendingReply>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>

#include <security/pam_appl.h>


/* Messages from the greeter to the server */
typedef enum
{
    GREETER_MESSAGE_CONNECT = 0,
    GREETER_MESSAGE_AUTHENTICATE,
    GREETER_MESSAGE_AUTHENTICATE_AS_GUEST,
    GREETER_MESSAGE_CONTINUE_AUTHENTICATION,
    GREETER_MESSAGE_START_SESSION,
    GREETER_MESSAGE_CANCEL_AUTHENTICATION,
    GREETER_MESSAGE_SET_LANGUAGE
} GreeterMessage;

/* Messages from the server to the greeter */
typedef enum
{
    SERVER_MESSAGE_CONNECTED = 0,
    SERVER_MESSAGE_PROMPT_AUTHENTICATION,
    SERVER_MESSAGE_END_AUTHENTICATION,
    SERVER_MESSAGE_SESSION_RESULT
} ServerMessage;

#define HEADER_SIZE 8

using namespace QLightDM;

class GreeterPrivate
{
public:
    QHash<QString, QString> hints;
    int toServerFd;
    int fromServerFd;
    QSocketNotifier *n;
    char *readBuffer;
    int nRead;
    bool inAuthentication;
    bool isAuthenticated;
    QString authenticationUser;
    int authenticateSequenceNumber;
    bool cancellingAuthentication;

    void writeInt(int value);
    void writeString(QString value);
    void writeHeader(int id, int length);
    void flush();
    char *readMessage(int *length, bool block);
};

Greeter::Greeter(QObject *parent) :
    QObject(parent),
    d(new GreeterPrivate)
{
    d->readBuffer = (char *)malloc(HEADER_SIZE);
    d->nRead = 0;
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

void GreeterPrivate::writeInt(int value)
{
    char buffer[4];
    buffer[0] = value >> 24;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;
    if (write(toServerFd, buffer, intLength()) != intLength()) {
        qDebug() << "Error writing to server";
    }
}

void GreeterPrivate::writeString(QString value)
{
    QByteArray a = value.toUtf8();
    writeInt(a.size());
    if (write(toServerFd, a.data(), a.size()) != a.size()) {
        qDebug() << "Error writing to server";
    }
}

void GreeterPrivate::writeHeader(int id, int length)
{
    writeInt(id);
    writeInt(length);
}

void GreeterPrivate::flush()
{
    fsync(toServerFd);
}

static int readInt(char *message, int messageLength, int *offset)
{
    if(messageLength - *offset < intLength()) {
        qDebug() << "Not enough space for int, need " << intLength() << ", got " << (messageLength - *offset);
        return 0;
    }

    char *buffer = message + *offset;
    int value = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
    *offset += intLength();
    return value;
}

static int getMessageLength(char *message, int messageLength)
{
    int offset = intLength();
    return readInt(message, messageLength, &offset);
}

static QString readString(char *message, int messageLength, int *offset)
{
    int length = readInt(message, messageLength, offset);
    if(messageLength - *offset < length) {
        qDebug() << "Not enough space for string, need " << length << ", got " << (messageLength - *offset);
        return "";
    }
    char *start = message + *offset;
    *offset += length;
    return QString::fromUtf8(start, length);
}

bool Greeter::connectSync()
{
    QDBusConnection busType = QDBusConnection::systemBus();
    QString ldmBus(qgetenv("LIGHTDM_BUS"));
    if(ldmBus == QLatin1String("SESSION")) {
        busType = QDBusConnection::sessionBus();
    }

    char* fd = getenv("LIGHTDM_TO_SERVER_FD");
    if(!fd) {
       qDebug() << "No LIGHTDM_TO_SERVER_FD environment variable";
       return false;
    }
    d->toServerFd = atoi(fd);

    qDebug() << "***connecting to server";
    QFile toServer;
    qDebug() << toServer.open(d->toServerFd, QIODevice::WriteOnly);

    fd = getenv("LIGHTDM_FROM_SERVER_FD");
    if(!fd) {
       qDebug() << "No LIGHTDM_FROM_SERVER_FD environment variable";
       return false;
    }
    d->fromServerFd = atoi(fd);

    d->n = new QSocketNotifier(d->fromServerFd, QSocketNotifier::Read);
    connect(d->n, SIGNAL(activated(int)), this, SLOT(onRead(int)));

    qDebug() << "Connecting to display manager...";
    d->writeHeader(GREETER_MESSAGE_CONNECT, stringLength(VERSION));
    d->writeString(VERSION);
    d->flush();

    int responseLength;
    char *response = d->readMessage(&responseLength, false);
    if (!response)
        return false;

    int offset = 0;
    int id = readInt(response, responseLength, &offset);
    int length = readInt(response, responseLength, &offset);
    bool connected = false;
    if (id == SERVER_MESSAGE_CONNECTED)
    {
        QString version = readString(response, responseLength, &offset);
        QString hintString = "";
        while (offset < length)
        {
            QString name = readString(response, responseLength, &offset);
            QString value = readString(response, responseLength, &offset);
            hintString.append (" ");
            hintString.append (name);
            hintString.append ("=");
            hintString.append (value);
        }

        qDebug() << "Connected version=" << version << hintString;
        connected = true;
    }
    else
        qDebug() << "Expected CONNECTED message, got " << id;
    free(response);

    return connected;
}

void Greeter::authenticate(const QString &username)
{
    d->inAuthentication = true;
    d->isAuthenticated = false;
    d->cancellingAuthentication = false;
    d->authenticationUser = username;
    qDebug() << "Starting authentication for user " << username << "...";
    d->writeHeader(GREETER_MESSAGE_AUTHENTICATE, intLength() + stringLength(username));
    d->authenticateSequenceNumber++;
    d->writeInt(d->authenticateSequenceNumber);
    d->writeString(username);
    d->flush();
}

void Greeter::authenticateAsGuest()
{
    d->authenticateSequenceNumber++;
    d->inAuthentication = true;
    d->isAuthenticated = false;
    d->cancellingAuthentication = false;
    d->authenticationUser = "";
    qDebug() << "Starting authentication for guest account";
    d->writeHeader(GREETER_MESSAGE_AUTHENTICATE_AS_GUEST, intLength());
    d->writeInt(d->authenticateSequenceNumber);
    d->flush();
}

void Greeter::respond(const QString &response)
{
    qDebug() << "Providing response to display manager";
    d->writeHeader(GREETER_MESSAGE_CONTINUE_AUTHENTICATION, intLength() + stringLength(response));
    // FIXME: Could be multiple response required
    d->writeInt(1);
    d->writeString(response);
    d->flush();
}

void Greeter::cancelAuthentication()
{
    qDebug() << "Cancelling authentication";
    d->cancellingAuthentication = true;
    d->writeHeader(GREETER_MESSAGE_CANCEL_AUTHENTICATION, 0);
    d->flush();
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

void Greeter::setLanguage (QString language)
{
    d->writeHeader(GREETER_MESSAGE_SET_LANGUAGE, stringLength(language));
    d->writeString (language);
    d->flush();
}

bool Greeter::startSessionSync(const QString &session)
{
    if (session.isEmpty()) {
        qDebug() << "Starting default session";
    }
    else {
        qDebug() << "Starting session " << session;
    }

    d->writeHeader(GREETER_MESSAGE_START_SESSION, stringLength(session));
    d->writeString(session);
    d->flush();

    int responseLength;
    char *response = d->readMessage(&responseLength, false);
    if (!response) {
        return false;
    }

    int offset = 0;
    int id = readInt(response, responseLength, &offset);
    readInt(response, responseLength, &offset);
    int returnCode = -1;
    if (id == SERVER_MESSAGE_SESSION_RESULT) {
        returnCode = readInt(response, responseLength, &offset);
    }
    else {
        qDebug() << "Expected SESSION_RESULT message, got " << id;
    }
    free(response);

    return returnCode == 0;
}

char* GreeterPrivate::readMessage(int *length, bool block)
{
    /* Read the header, or the whole message if we already have that */
    int nToRead = HEADER_SIZE;
    if(nRead >= HEADER_SIZE) {
        nToRead += getMessageLength(readBuffer, nRead);
    }

    do {
        ssize_t nRead = read(fromServerFd, readBuffer + nRead, nToRead - nRead);
        if(nRead < 0) {
            qDebug() << "Error reading from server";
            return NULL;
        }

        if (nRead == 0) {
            qDebug() << "EOF reading from server";
            return NULL;
        }

        qDebug() << "Read " << nRead << " octets from daemon";
        nRead += nRead;
    } while(nRead < nToRead && block);

    /* Stop if haven't got all the data we want */  
    if(nRead != nToRead) {
        return NULL;
    }

    /* If have header, rerun for content */
    if(nRead == HEADER_SIZE) {
        nToRead = getMessageLength(readBuffer, nRead);
        if(nToRead > 0) {
            readBuffer = (char *)realloc(readBuffer, HEADER_SIZE + nToRead);
            return readMessage(length, block);
        }
    }

    char *buffer = readBuffer;
    *length = nRead;

    readBuffer = (char *)malloc(nRead);
    nRead = 0;

    return buffer;
}

void Greeter::onRead(int fd)
{
    qDebug() << "Reading from server";

    int messageLength;
    char *message = d->readMessage(&messageLength, false);
    if (!message) {
        return;
    }

    int offset = 0;
    int id = readInt(message, messageLength, &offset);
    int length = readInt(message, messageLength, &offset);
    int nMessages, sequenceNumber, returnCode;
    QString version, username;
    switch(id) {
    case SERVER_MESSAGE_PROMPT_AUTHENTICATION:
        sequenceNumber = readInt(message, messageLength, &offset);
        username = readString(message, messageLength, &offset);

        d->authenticationUser = username;

        if (sequenceNumber == d->authenticateSequenceNumber &&
            !d->cancellingAuthentication)
        {
            nMessages = readInt(message, messageLength, &offset);
            qDebug() << "Prompt user with " << nMessages << " message(s)";
            for(int i = 0; i < nMessages; i++)
            {
                int style = readInt(message, messageLength, &offset);
                QString text = readString(message, messageLength, &offset);

                // FIXME: Should stop on prompts?
                switch (style)
                {
                case PAM_PROMPT_ECHO_OFF:
                    emit showPrompt(text, Greeter::PromptTypeSecret);
                    break;
                case PAM_PROMPT_ECHO_ON:
                    emit showPrompt(text, Greeter::PromptTypeQuestion);
                    break;
                case PAM_ERROR_MSG:
                    emit showMessage(text, Greeter::MessageTypeError);
                    break;
                case PAM_TEXT_INFO:
                    emit showMessage(text, Greeter::MessageTypeInfo);
                    break;
                }
            }
        }
        break;
    case SERVER_MESSAGE_END_AUTHENTICATION:
        sequenceNumber = readInt(message, messageLength, &offset);
        username = readString(message, messageLength, &offset);
        returnCode = readInt(message, messageLength, &offset);

        if (sequenceNumber == d->authenticateSequenceNumber)
        {
            qDebug() << "Authentication complete with return code " << returnCode;

            d->cancellingAuthentication = false;
            d->isAuthenticated = (returnCode == 0);
            d->authenticationUser = username;
            d->inAuthentication = false;
            emit authenticationComplete();
        }
        else
            qDebug () << "Ignoring end authentication with invalid sequence number " << sequenceNumber;
        break;
    default:
        qDebug() << "Unknown message from server: " << id;
    }
    free(message);
}

QString Greeter::getHint(QString name) const
{
    return d->hints.value (name);
}

QString Greeter::defaultSessionHint() const
{
    return getHint ("default-session");
}

bool Greeter::hideUsersHint() const
{
    return d->hints.value ("hide-users", "true") == "true";
}

bool Greeter::hasGuestAccountHint() const
{
    return d->hints.value ("has-guest-account", "false") == "true";
}

QString Greeter::selectUserHint() const
{
    return getHint ("select-user");
}

bool Greeter::selectGuestHint() const
{
    return d->hints.value ("select-guest", "false") == "true";
}

QString Greeter::autologinUserHint() const
{
    return getHint ("autologin-user");
}

bool Greeter::autologinGuestHint() const
{
    return d->hints.value ("autologin-guest", "false") == "true";
}

int Greeter::autologinTimeoutHint() const
{
    return d->hints.value ("autologin-timeout", "0").toInt ();
}

#include "greeter_moc.cpp"
