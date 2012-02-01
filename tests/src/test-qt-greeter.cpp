#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <xcb/xcb.h>
#include <QLightDM/Greeter>
#include <QtCore/QSettings>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>

#include "test-qt-greeter.h"
#include "status.h"

static QSettings *config = NULL;

TestGreeter::TestGreeter ()
{
    connect (this, SIGNAL(showMessage(QString, QLightDM::Greeter::MessageType)), SLOT(showMessage(QString, QLightDM::Greeter::MessageType)));
    connect (this, SIGNAL(showPrompt(QString, QLightDM::Greeter::PromptType)), SLOT(showPrompt(QString, QLightDM::Greeter::PromptType)));
    connect (this, SIGNAL(authenticationComplete()), SLOT(authenticationComplete()));
}

void TestGreeter::showMessage (QString text, QLightDM::Greeter::MessageType type)
{
    notify_status ("GREETER %s SHOW-MESSAGE TEXT=\"%s\"", getenv ("DISPLAY"), text.toAscii ().constData ());
}

void TestGreeter::showPrompt (QString text, QLightDM::Greeter::PromptType type)
{
    notify_status ("GREETER %s SHOW-PROMPT TEXT=\"%s\"", getenv ("DISPLAY"), text.toAscii ().constData ());

    QString username = config->value ("test-greeter-config/username").toString ();
    QString password = config->value ("test-greeter-config/password").toString ();

    QString response;
    if (config->value ("test-greeter-config/prompt-username", "false") == "true" && text == "login:")
        response = username;
    else if (password != "")
        response = password;

    if (response != "")
    {
        notify_status ("GREETER %s RESPOND TEXT=\"%s\"", getenv ("DISPLAY"), response.toAscii ().constData ());
        respond (response);
    }
}

void TestGreeter::authenticationComplete ()
{
    if (authenticationUser () != "")
        notify_status ("GREETER %s AUTHENTICATION-COMPLETE USERNAME=%s AUTHENTICATED=%s",
                       getenv ("DISPLAY"),
                       authenticationUser ().toAscii ().constData (), isAuthenticated () ? "TRUE" : "FALSE");
    else
        notify_status ("GREETER %s AUTHENTICATION-COMPLETE AUTHENTICATED=%s", getenv ("DISPLAY"), isAuthenticated () ? "TRUE" : "FALSE");
    if (!isAuthenticated ())
        return;

    if (!startSessionSync (config->value ("test-greeter-config/session").toString ()))
        notify_status ("GREETER %s SESSION-FAILED", getenv ("DISPLAY"));
}

static void
signal_cb (int signum)
{
    notify_status ("GREETER %s TERMINATE SIGNAL=%d", getenv ("DISPLAY"), signum);
    exit (EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    notify_status ("GREETER %s START", getenv ("DISPLAY"));

    if (getenv ("LIGHTDM_TEST_CONFIG"))
        config = new QSettings(getenv ("LIGHTDM_TEST_CONFIG"), QSettings::IniFormat);
    else
        config = new QSettings();

    xcb_connection_t *connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        notify_status ("GREETER %s FAIL-CONNECT-XSERVER", getenv ("DISPLAY"));
        return EXIT_FAILURE;
    }

    notify_status ("GREETER %s CONNECT-XSERVER", getenv ("DISPLAY"));

    TestGreeter *greeter = new TestGreeter();
  
    notify_status ("GREETER %s CONNECT-TO-DAEMON", getenv ("DISPLAY"));
    if (!greeter->connectSync())
    {
        notify_status ("GREETER %s FAIL-CONNECT-DAEMON", getenv ("DISPLAY"));
        return EXIT_FAILURE;
    }

    notify_status ("GREETER %s CONNECTED-TO-DAEMON", getenv ("DISPLAY"));

    if (greeter->selectUserHint() != "")
    {
        notify_status ("GREETER %s AUTHENTICATE-SELECTED USERNAME=%s", getenv ("DISPLAY"), greeter->selectUserHint ().toAscii ().constData ());
        greeter->authenticate (greeter->selectUserHint ());
    }
    else
    {
        QString login_lock = QString (getenv ("LIGHTDM_TEST_HOME_DIR")) + "/.greeter-logged-in";
        FILE *f = fopen (login_lock.toAscii (), "r");
        if (f == NULL)
        {
            if (config->value ("test-greeter-config/login-guest", "false") == "true")
            {
                notify_status ("GREETER %s AUTHENTICATE-GUEST", getenv ("DISPLAY"));
                greeter->authenticateAsGuest ();
            }
            else if (config->value ("test-greeter-config/prompt-username", "false") == "true")
            {
                notify_status ("GREETER %s AUTHENTICATE", getenv ("DISPLAY"));
                greeter->authenticate ();
            }
            else
            {
                QString username = config->value ("test-greeter-config/username").toString ();
                if (username != "")
                {
                    notify_status ("GREETER %s AUTHENTICATE USERNAME=%s", getenv ("DISPLAY"), username.toAscii ().constData ());
                    greeter->authenticate (username);
                }
            }

            /* Write lock to stop repeatedly logging in */
            f = fopen (login_lock.toAscii (), "w");
            fclose (f);
        }
        else
        {
            qDebug () << "Not logging in, lock file detected " << login_lock;
            fclose (f);
        }
    }

    return app.exec();
}
