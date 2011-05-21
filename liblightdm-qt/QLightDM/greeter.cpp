#include "greeter.h"

#include "user.h"
#include "sessionsmodel.h"

#include <security/pam_appl.h>
#include <pwd.h>
#include <errno.h>

#include <QtNetwork/QHostInfo> //needed for localHostName
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QVariant>
#include <QtCore/QFile>
#include <QtDBus/QDBusPendingReply>


typedef enum
{
    /* Messages from the greeter to the server */
    GREETER_MESSAGE_CONNECT                 = 1,
    GREETER_MESSAGE_START_AUTHENTICATION    = 2,
    GREETER_MESSAGE_CONTINUE_AUTHENTICATION = 3,
    GREETER_MESSAGE_LOGIN                   = 4,
    GREETER_MESSAGE_CANCEL_AUTHENTICATION   = 5,

    /* Messages from the server to the greeter */
    GREETER_MESSAGE_CONNECTED               = 101,
    GREETER_MESSAGE_QUIT                    = 102,
    GREETER_MESSAGE_PROMPT_AUTHENTICATION   = 103,
    GREETER_MESSAGE_END_AUTHENTICATION      = 104
} GreeterMessage;

#define HEADER_SIZE 8

using namespace QLightDM;

class GreeterPrivate
{
public:
    QString theme;
    QString defaultLayout;
    QString defaultSession;
    QString timedUser;
    int loginDelay;
    
    SessionsModel *sessionsModel;

    QSettings *config;
    bool haveConfig;

    QList<User*> users;
    bool haveUsers;

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
};


Greeter::Greeter(QObject *parent) :
    QObject(parent),
    d(new GreeterPrivate)
{
    d->readBuffer = (char *)malloc (HEADER_SIZE);
    d->nRead = 0;
    d->haveConfig = false;
    d->haveUsers = false;
    
    d->sessionsModel = new SessionsModel(this);
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


    d->lightdmInterface = new QDBusInterface("org.lightdm.LightDisplayManager", "/org/lightdm/LightDisplayManager", "org.lightdm.LightDisplayManager", busType);
    d->powerManagementInterface = new QDBusInterface("org.freedesktop.PowerManagement","/org/freedesktop/PowerManagement", "org.freedesktop.PowerManagement");
    d->consoleKitInterface = new QDBusInterface("org.freedesktop.ConsoleKit", "/org/freedesktop/ConsoleKit/Manager", "org.freedesktop.ConsoleKit");

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

void Greeter::startAuthentication(const QString &username)
{
    d->inAuthentication = true;
    d->isAuthenticated = false;
    d->authenticationUser = username;
    qDebug() << "Starting authentication for user " << username << "...";
    writeHeader(GREETER_MESSAGE_START_AUTHENTICATION, stringLength(username));
    writeString(username);
    flush();     
}

void Greeter::provideSecret(const QString &secret)
{
    qDebug() << "Providing secret to display manager";
    writeHeader(GREETER_MESSAGE_CONTINUE_AUTHENTICATION, intLength() + stringLength(secret));
    // FIXME: Could be multiple secrets required
    writeInt(1);
    writeString(secret);
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

void Greeter::login(const QString &username, const QString &session, const QString &language)
{
    qDebug() << "Logging in as " << username << " for session " << session << " with language " << language;
    writeHeader(GREETER_MESSAGE_LOGIN, stringLength(username) + stringLength(session) + stringLength(language));
    writeString(username);
    writeString(session);
    writeString(language);
    flush();
}

void Greeter::loginWithDefaults(const QString &username)
{
    login(username, NULL, NULL);
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
    int nMessages, returnCode;
    switch(id)
    {
    case GREETER_MESSAGE_CONNECTED:
        d->theme = readString(&offset);
        d->defaultLayout = readString(&offset);
        d->defaultSession = readString(&offset);
        d->timedUser = readString(&offset);
        d->loginDelay = readInt(&offset);
        qDebug() << "Connected theme=" << d->theme << " default-layout=" << d->defaultLayout << " default-session=" << d->defaultSession << " timed-user=" << d->timedUser << " login-delay" << d->loginDelay;

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
        returnCode = readInt(&offset);
        qDebug() << "Authentication complete with return code " << returnCode;
        d->isAuthenticated = (returnCode == 0);
        if(!d->isAuthenticated) {
            d->authenticationUser = "";
        }
        emit authenticationComplete(d->isAuthenticated);
        d->inAuthentication = false;
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
    return QVariant(); //FIXME TODO
}

QString Greeter::defaultLanguage() const
{
    return getenv("LANG");
}

QString Greeter::defaultLayout() const
{
    return d->defaultLayout;
}

QString Greeter::defaultSession() const
{
    return d->defaultSession;
}

QString Greeter::timedLoginUser() const
{
    return d->timedUser;
}

int Greeter::timedLoginDelay() const
{
    return d->loginDelay;
}

void Greeter::loadConfig()
{
    if(d->haveConfig)
        return;
  
     QString file;
     if(false)
       file = d->lightdmInterface->property("ConfigFile").toString();
     qDebug() << "Loading configuration from " << file;
     d->config = new QSettings(file, QSettings::IniFormat);

    d->haveConfig = true;
}

void Greeter::loadUsers()
{
    QStringList hiddenUsers, hiddenShells;
    int minimumUid;
    QList<User*> users, oldUsers, newUsers, changedUsers;

    loadConfig();

    if(d->config->contains("UserManager/minimum-uid"))
        minimumUid = d->config->value("UserManager/minimum-uid").toInt();
    else
        minimumUid = 500;

    if (d->config->contains("UserManager/hidden-shells"))
        hiddenUsers = d->config->value("UserManager/hidden-shells").toString().split(" ");
    else
        hiddenUsers << "nobody" << "nobody4" << "noaccess";

    if (d->config->contains("UserManager/hidden-shells"))
        hiddenShells = d->config->value("UserManager/hidden-shells").toString().split(" ");
    else
        hiddenShells << "/bin/false" << "/usr/sbin/nologin";

    setpwent();

    while(TRUE)
    {
        struct passwd *entry;
        User *user;
        QStringList tokens;
        QString realName, image;
        QFile *imageFile;
        int i;

        errno = 0;
        entry = getpwent();
        if(!entry)
            break;

        /* Ignore system users */
        if(entry->pw_uid < minimumUid)
            continue;

        /* Ignore users disabled by shell */
        if(entry->pw_shell)
        {
            for(i = 0; i < hiddenShells.size(); i++)
                if(entry->pw_shell == hiddenShells.at(i))
                    break;
            if(i < hiddenShells.size())
                continue;
        }

        /* Ignore certain users */
        for(i = 0; i < hiddenUsers.size(); i++)
            if(entry->pw_name == hiddenUsers.at(i))
                break;
        if(i < hiddenUsers.size())
            continue;
 
        tokens = QString(entry->pw_gecos).split(",");
        if(tokens.size() > 0 && tokens.at(i) != "")
            realName = tokens.at(i);
      
        QDir homeDir(entry->pw_dir);
        imageFile = new QFile(homeDir.filePath(".face"));
        if(!imageFile->exists())
        {
            delete imageFile;
            imageFile = new QFile(homeDir.filePath(".face.icon"));
        }
        if(imageFile->exists())
            image = "file://" + imageFile->fileName();
        delete imageFile;

        user = new User(entry->pw_name, realName, entry->pw_dir, image, FALSE);

        /* Update existing users if have them */
        bool matchedUser = false;
        foreach(User *info, d->users)
        {
            if(info->name() == user->name())
            {
                matchedUser = true;
                if(info->update(user->realName(), user->homeDirectory(), user->image(), user->isLoggedIn()))
                    changedUsers.append(user);
                delete user;
                user = info;
                break;
            }
        }
        if(!matchedUser)
        {
            /* Only notify once we have loaded the user list */
            if(d->haveUsers)
                newUsers.append(user);
        }
        users.append(user);
    }

    if(errno != 0)
        qDebug() << "Failed to read password database: " << strerror(errno);

    endpwent();

    /* Use new user list */
    oldUsers = d->users;
    d->users = users;

    /* Notify of changes */
    foreach(User *user, newUsers)
    {
        qDebug() << "User " << user->name() << " added";
        emit userAdded(user);
    }

    foreach(User *user, changedUsers)
    {
        qDebug() << "User " << user->name() << " changed";
        emit userChanged(user);
    }

    foreach(User *user, oldUsers)
    {
        /* See if this user is in the current list */
        bool existing = false;
        foreach(User *new_user, d->users)
        {
            if (new_user == user)
            {
                existing = true;
                break;
            }
        }

        if(!existing)
        {
            qDebug() << "User " << user->name() << " removed";
            emit userRemoved(user);
            delete user;
        }
    }
}

void Greeter::updateUsers()
{
    if (d->haveUsers) {
        return;
    }
  
    loadConfig();

    /* User listing is disabled */
    if (d->config->contains("UserManager/load-users") &&
        !d->config->value("UserManager/load-users").toBool())
    {
        d->haveUsers = true;
        return;
    }

    loadUsers();

    d->haveUsers = true;
}

QList<User*> Greeter::users()
{
    updateUsers();
    return d->users;
}

SessionsModel* Greeter::sessionsModel() const
{
    return d->sessionsModel; 
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
