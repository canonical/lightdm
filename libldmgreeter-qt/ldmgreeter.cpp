#include "ldmgreeter.h"

#include "powermanagementinterface.h"
#include "displayinterface.h"
#include "usermanagerinterface.h"
#include "consolekitinterface.h"
#include "ldmuser.h"
#include "ldmsession.h"

#include <QtNetwork/QHostInfo> //needed for localHostName
#include <QDebug>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusPendingReply>
#include <QtGui/QApplication>
#include <QtGui/QDesktopWidget>

class LdmGreeterPrivate
{
public:
    QString language;
    QString layout;
    QString session;
    QString username;
    QString themeName; //TODO turn into a KConfig
    int delay;

    QString currentlyAuthenticatingUser;

    PowerManagementInterface* powerManagement;
    DisplayInterface* display;
    UserManagerInterface* userManager;
    ConsoleKitInterface* consoleKit;
};



LdmGreeter::LdmGreeter() :
    QWidget(0),
    d(new LdmGreeterPrivate)
{
    QRect screen = QApplication::desktop()->rect();
    this->setGeometry(screen);

    qDBusRegisterMetaType<LdmUser>();
    qDBusRegisterMetaType<QList<LdmUser> >();

    qDBusRegisterMetaType<LdmAuthRequest>();
    qDBusRegisterMetaType<QList<LdmAuthRequest> >();

    d->powerManagement = new PowerManagementInterface("org.freedesktop.PowerManagement","/org/freedesktop/PowerManagement", QDBusConnection::sessionBus(), this);
    d->display = new DisplayInterface("org.lightdm.LightDisplayManager", "/org/lightdm/LightDisplayManager/Display0", QDBusConnection::sessionBus(), this);
    d->userManager = new UserManagerInterface("org.lightdm.LightDisplayManager", "/org/lightdm/LightDisplayManager/Users", QDBusConnection::sessionBus(), this);
    d->consoleKit = new ConsoleKitInterface("org.freedesktop.ConsoleKit","/org/freedesktop/ConsoleKit/Manager", QDBusConnection::systemBus(), this );

    //FIXME use the pendingReply, it's a much nicer API.
    QDBusReply<QString> connectResult = d->display->Connect(d->language, d->layout, d->session, d->username, d->delay);
    connect(d->display, SIGNAL(quitGreeter()), SIGNAL(quit()));


    if(!connectResult.isValid())
    {
        qDebug() << connectResult.error().name();
    }
    else
    {
        d->themeName = connectResult;
        //TODO create a kconfig from this path name - or not. Keep this lib Qt only?
    }
}

LdmGreeter::~LdmGreeter()
{
    delete d;
}



QString LdmGreeter::hostname()
{
    return QHostInfo::localHostName();
}

QString LdmGreeter::defaultLanguage()
{
    return d->language;
}

QString LdmGreeter::defaultLayout()
{
    return d->layout;
}

QString LdmGreeter::defaultSession()
{
    return d->session;
}

QString LdmGreeter::defaultUsername()
{
    return d->username;
}


QList<LdmUser> LdmGreeter::users()
{
    QDBusPendingReply<QList<LdmUser> > users = d->userManager->GetUsers();
    users.waitForFinished();
    if (users.isValid())
    {
        return users.value();
    }
    else
    {
        qDebug() << users.error().name();
        qDebug() << users.error().message();
        return QList<LdmUser>();
    }
}

QList<LdmSession> LdmGreeter::sessions()
{
    QList<LdmSession> sessions;
    //FIXME don't hardcode this!
    //FIXME I'm not happy with this bodgy finding .desktop files, and strcat situtation.
    QDir sessionDir("/usr/share/xsessions");
    sessionDir.setNameFilters(QStringList() << "*.desktop");
    foreach(QString sessionFileName, sessionDir.entryList())
    {
        QSettings sessionData(QString("/usr/share/xsessions/").append(sessionFileName), QSettings::IniFormat);
        sessionData.beginGroup("Desktop Entry");
        sessionFileName.chop(8);// chop(8) removes '.desktop'
        QString name = sessionData.value("Name").toString();
        QString comment = sessionData.value("Comment").toString();
        LdmSession session(sessionFileName, name, comment);

        sessions.append(session);
    }
    return sessions;
}


void LdmGreeter::startAuthentication(QString username)
{
    d->currentlyAuthenticatingUser = username;
    QDBusPendingReply<int, QList<LdmAuthRequest> > reply = d->display->StartAuthentication(username);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)), SLOT(onAuthFinished(QDBusPendingCallWatcher*)));
}

void LdmGreeter::provideSecret(QString secret)
{
    QDBusPendingReply<int, QList<LdmAuthRequest> > reply = d->display->ContinueAuthentication(QStringList() << secret);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)), SLOT(onAuthFinished(QDBusPendingCallWatcher*)));
}

void LdmGreeter::login(QString username, QString session, QString language)
{
    d->display->Login(username, session, language);
}


bool LdmGreeter::canSuspend()
{
    QDBusPendingReply<bool> reply = d->powerManagement->canSuspend();
    reply.waitForFinished();
    return reply.value();
}

void LdmGreeter::suspend()
{
    d->powerManagement->suspend();
}

bool LdmGreeter::canHibernate()
{
    QDBusPendingReply<bool> reply = d->powerManagement->canHibernate();
    reply.waitForFinished();
    return reply.value();
}

void LdmGreeter::hibernate()
{
    d->powerManagement->hibernate();
}

bool LdmGreeter::canShutdown()
{
    QDBusPendingReply<bool> reply = d->consoleKit->canStop();
    reply.waitForFinished();
    return reply.value();
}


void LdmGreeter::shutdown()
{
    d->consoleKit->stop();
}

bool LdmGreeter::canRestart()
{
    QDBusPendingReply<bool> reply = d->consoleKit->canRestart();
    reply.waitForFinished();
        return reply.value();
}

void LdmGreeter::restart()
{
    d->consoleKit->restart();
}


void LdmGreeter::onAuthFinished(QDBusPendingCallWatcher *call)
{
    QDBusPendingReply<int, QList<LdmAuthRequest> > reply = *call;
    if(reply.isValid())
    {
        QList<LdmAuthRequest> requests = reply.argumentAt<1>();

        if (requests.size() == 0) //if there are no requests
        {
            int returnValue = reply.argumentAt<0>();
            if (returnValue == 0) //Magic number..FIXME
            {
                emit authenticationComplete(true);
            }
            else
            {
                emit authenticationComplete(false);
            }
        }
        else
        {
            foreach(LdmAuthRequest request, requests)
            {
                switch(request.messageType())
                {
                    //FIXME create an enum use that (or include PAM libs)
                case 0:
                case 1:
                    emit showPrompt(request.message());
                    break;
                case 2:
                    emit showMessage(request.message());
                    break;
                case 3:
                    emit showError(request.message());
                    break;
                }
            }
        }
    }
    else
    {
        qDebug() << reply.error().name();
        qDebug() << reply.error().message();
    }
}
