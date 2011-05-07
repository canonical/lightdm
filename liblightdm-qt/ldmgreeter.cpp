#include "ldmgreeter.h"

#include "ldmuser.h"
#include "ldmsession.h"

#include <security/pam_appl.h>
#include <pwd.h>
#include <errno.h>

#include <QtNetwork/QHostInfo> //needed for localHostName
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QVariant>
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

class LdmGreeterPrivate
{
public:
    QString theme;
    QString defaultLayout;
    QString defaultSession;
    QString timedUser;
    int loginDelay;

    QSettings *config;
    bool haveConfig;

    QList<LdmUser*> users;
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


LdmGreeter::LdmGreeter(QObject *parent) :
    QObject(parent),
    d(new LdmGreeterPrivate)
{
    d->readBuffer = (char *)malloc (HEADER_SIZE);
    d->nRead = 0;
}

LdmGreeter::~LdmGreeter()
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

void LdmGreeter::writeInt(int value)
{
    char buffer[4];
    buffer[0] = value >> 24;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;   
    if (write(d->toServerFd, buffer, intLength()) != intLength())
    {
        qDebug() << "Error writing to server";
    }
}

void LdmGreeter::writeString(QString value)
{
    QByteArray a = value.toUtf8();
    writeInt(a.size());
    if (write(d->toServerFd, a.data(), a.size()) != a.size())
    {
        qDebug() << "Error writing to server";
    }
}

void LdmGreeter::writeHeader(int id, int length)
{
    writeInt(id);
    writeInt(length);
}

void LdmGreeter::flush()
{
    fsync(d->toServerFd);
}

int LdmGreeter::getPacketLength()
{
    int offset = intLength();
    return readInt(&offset);
}

int LdmGreeter::readInt(int *offset)
{
    if(d->nRead - *offset < intLength())
    {
        qDebug() << "Not enough space for int, need " << intLength() << ", got " << (d->nRead - *offset);
        return 0;
    }
    char *buffer = d->readBuffer + *offset;
    int value = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
    *offset += intLength();
    return value;
}

QString LdmGreeter::readString(int *offset)
{
    int length = readInt(offset);
    if(d->nRead - *offset < length)
    {
        qDebug() << "Not enough space for string, need " << length << ", got " << (d->nRead - *offset);
        return "";
    }
    char *start = d->readBuffer + *offset;
    *offset += length;
    return QString::fromUtf8(start, length);
}

void LdmGreeter::connectToServer()
{
    QDBusConnection busType = QDBusConnection::systemBus();
    QString ldmBus(qgetenv("LDM_BUS"));
    if(ldmBus == QLatin1String("SESSION"))
        busType = QDBusConnection::sessionBus();

    d->lightdmInterface = new QDBusInterface("org.lightdm.LightDisplayManager", "/org/lightdm/LightDisplayManager", "org.lightdm.LightDisplayManager", busType);
    d->powerManagementInterface = new QDBusInterface("org.freedesktop.PowerManagement","/org/freedesktop/PowerManagement", "org.freedesktop.PowerManagement");
    d->consoleKitInterface = new QDBusInterface("org.freedesktop.ConsoleKit", "/org/freedesktop/ConsoleKit/Manager", "org.freedesktop.ConsoleKit");

    char* fd = getenv("LDM_TO_SERVER_FD");
    if(!fd)
    {
       qDebug() << "No LDM_TO_SERVER_FD environment variable";
       return;
    }
    d->toServerFd = atoi(fd);

    fd = getenv("LDM_FROM_SERVER_FD");
    if(!fd)
    {
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

void LdmGreeter::startAuthentication(const QString &username)
{
    d->inAuthentication = true;
    d->isAuthenticated = false;
    d->authenticationUser = username;
    qDebug() << "Starting authentication for user " << username << "...";
    writeHeader(GREETER_MESSAGE_START_AUTHENTICATION, stringLength(username));
    writeString(username);
    flush();     
}

void LdmGreeter::provideSecret(const QString &secret)
{
    qDebug() << "Providing secret to display manager";
    writeHeader(GREETER_MESSAGE_CONTINUE_AUTHENTICATION, intLength() + stringLength(secret));
    // FIXME: Could be multiple secrets required
    writeInt(1);
    writeString(secret);
    flush();
}

void LdmGreeter::cancelAuthentication()
{
    qDebug() << "Cancelling authentication";
    writeHeader(GREETER_MESSAGE_CANCEL_AUTHENTICATION, 0);
    flush();  
}

bool LdmGreeter::inAuthentication() const
{
    return d->inAuthentication;
}

bool LdmGreeter::isAuthenticated() const
{
    return d->isAuthenticated;
}

QString LdmGreeter::authenticationUser() const
{
    return d->authenticationUser;
}

void LdmGreeter::login(const QString &username, const QString &session, const QString &language)
{
    qDebug() << "Logging in as " << username << " for session " << session << " with language " << language;
    writeHeader(GREETER_MESSAGE_LOGIN, stringLength(username) + stringLength(session) + stringLength(language));
    writeString(username);
    writeString(session);
    writeString(language);
    flush();
}

void LdmGreeter::loginWithDefaults(const QString &username)
{
    login(username, NULL, NULL);
}

void LdmGreeter::onRead(int fd)
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
        if(!d->isAuthenticated)
            d->authenticationUser = "";
        emit authenticationComplete();
        d->inAuthentication = false;
        break;
    default:
        qDebug() << "Unknown message from server: " << id;
    }

    d->nRead = 0;
}

QString LdmGreeter::hostname() const
{
    return QHostInfo::localHostName();
}

QString LdmGreeter::theme() const
{
    return d->theme;
}

QVariant LdmGreeter::getProperty(const QString &name) const
{
    return QVariant(); //FIXME TODO
}

QString LdmGreeter::defaultLanguage() const
{
    return getenv("LANG");
}

QString LdmGreeter::defaultLayout() const
{
    return d->defaultLayout;
}

QString LdmGreeter::defaultSession() const
{
    return d->defaultSession;
}

QString LdmGreeter::timedLoginUser() const
{
    return d->timedUser;
}

int LdmGreeter::timedLoginDelay() const
{
    return d->loginDelay;
}

void LdmGreeter::loadConfig()
{
    if(d->haveConfig)
        return;

    //FIXME: Need to get property QDBusReply<QString> reply = d->lightdmInterface.call("

    d->config = new QSettings(/*FIXME*/"/etc/lightdm.conf", QSettings::IniFormat);
    d->haveConfig = true;
}

void LdmGreeter::loadUsers()
{
    QStringList hiddenUsers, hiddenShells;
    int minimumUid;
    QList<LdmUser*> users, oldUsers, newUsers, changedUsers;

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
        LdmUser *user;
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

        user = new LdmUser(entry->pw_name, realName, entry->pw_dir, image, FALSE);

        /* Update existing users if have them */
        bool matchedUser = false;
        foreach(LdmUser *info, d->users)
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
    foreach(LdmUser *user, newUsers)
    {
        qDebug() << "User " << user->name() << " added";
        emit userAdded(user);
    }
    foreach(LdmUser *user, changedUsers)
    {
        qDebug() << "User " << user->name() << " changed";
        emit userChanged(user);
    }
    foreach(LdmUser *user, oldUsers)
    {
        /* See if this user is in the current list */
        bool existing = false;
        foreach(LdmUser *new_user, d->users)
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

void LdmGreeter::updateUsers()
{
    if (d->haveUsers)
        return;
  
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

QList<LdmUser*> LdmGreeter::users()
{
    updateUsers();
    return d->users;
}

QList<LdmSession> LdmGreeter::sessions() const
{
    QList<LdmSession> sessions;
    //FIXME don't hardcode this!
    QDir sessionDir("/usr/share/xsessions");
    sessionDir.setNameFilters(QStringList() << "*.desktop");
    foreach(QString sessionFileName, sessionDir.entryList())
    {
        QSettings sessionData(sessionDir.filePath(sessionFileName), QSettings::IniFormat);
        sessionFileName.chop(8);// chop(8) removes '.desktop'
        QString name = sessionData.value("DesktopEntry/Name").toString();
        QString comment = sessionData.value("DesktopEntry/Comment").toString();
        LdmSession session(sessionFileName, name, comment);

        sessions.append(session);
    }
    return sessions;
}

bool LdmGreeter::canSuspend() const
{
    QDBusReply<bool> reply = d->powerManagementInterface->call("CanSuspend");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void LdmGreeter::suspend()
{
    d->powerManagementInterface->call("Suspend");
}

bool LdmGreeter::canHibernate() const
{
    QDBusReply<bool> reply = d->powerManagementInterface->call("CanHibernate");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void LdmGreeter::hibernate()
{
    d->powerManagementInterface->call("Hibernate");
}

bool LdmGreeter::canShutdown() const
{
    QDBusReply<bool> reply = d->consoleKitInterface->call("CanStop");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void LdmGreeter::shutdown()
{
    d->consoleKitInterface->call("stop");
}

bool LdmGreeter::canRestart() const
{
    QDBusReply<bool> reply = d->consoleKitInterface->call("CanRestart");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void LdmGreeter::restart()
{
    d->consoleKitInterface->call("Restart");
}
