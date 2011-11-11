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
    connect (this, SIGNAL(showMessage(QString, Greeter::MessageType)), SLOT(showMessage(QString, Greeter::MessageType)));
    connect (this, SIGNAL(showPrompt(QString, Greeter::PromptType)), SLOT(showPrompt(QString, Greeter::PromptType)));
    connect (this, SIGNAL(authenticationComplete()), SLOT(authenticationComplete()));
}

void TestGreeter::showMessage (QString text, Greeter::MessageType type)
{
    notify_status ("GREETER SHOW-MESSAGE TEXT=\"%s\"", text.toAscii ().constData ());
}

void TestGreeter::showPrompt (QString text, Greeter::PromptType type)
{
    notify_status ("GREETER SHOW-PROMPT TEXT=\"%s\"", text.toAscii ().constData ());

    QString username = config->value ("test-greeter-config/username").toString ();
    QString password = config->value ("test-greeter-config/password").toString ();

    QString response;
    if (config->value ("test-greeter-config/prompt-username", "false") == "true" && text == "login:")
        response = username;
    else if (password != "")
        response = password;

    if (response != "")
    {
        notify_status ("GREETER RESPOND TEXT=\"%s\"", response.toAscii ().constData ());
        respond (response);
    }
}

void TestGreeter::authenticationComplete ()
{
    if (authenticationUser () != "")
        notify_status ("GREETER AUTHENTICATION-COMPLETE USERNAME=%s AUTHENTICATED=%s",
                       authenticationUser ().toAscii ().constData (), isAuthenticated () ? "TRUE" : "FALSE");
    else
        notify_status ("GREETER AUTHENTICATION-COMPLETE AUTHENTICATED=%s", isAuthenticated () ? "TRUE" : "FALSE");
    if (!isAuthenticated ())
        return;

    if (!startSessionSync (config->value ("test-greeter-config/session").toString ()))
        notify_status ("GREETER SESSION-FAILED");
}

static void
signal_cb (int signum)
{
    notify_status ("GREETER TERMINATE SIGNAL=%d", signum);
    exit (EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    notify_status ("GREETER START");

    if (getenv ("LIGHTDM_TEST_CONFIG"))
        config = new QSettings(getenv ("LIGHTDM_TEST_CONFIG"), QSettings::IniFormat);
    else
        config = new QSettings();

    xcb_connection_t *connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        notify_status ("GREETER FAIL-CONNECT-XSERVER %s", getenv ("DISPLAY"));
        return EXIT_FAILURE;
    }

    notify_status ("GREETER CONNECT-XSERVER %s", getenv ("DISPLAY"));

    TestGreeter *greeter = new TestGreeter();
  
    notify_status ("GREETER CONNECT-TO-DAEMON");  
    if (!greeter->connectSync())
    {
        notify_status ("GREETER FAIL-CONNECT-DAEMON");
        return EXIT_FAILURE;
    }

    notify_status ("GREETER CONNECTED-TO-DAEMON");

    if (greeter->selectUserHint() != "")
    {
        notify_status ("GREETER AUTHENTICATE-SELECTED USERNAME=%s", greeter->selectUserHint ().toAscii ().constData ());
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
                notify_status ("GREETER AUTHENTICATE-GUEST");
                greeter->authenticateAsGuest ();
            }
            else if (config->value ("test-greeter-config/prompt-username", "false") == "true")
            {
                notify_status ("GREETER AUTHENTICATE");
                greeter->authenticate ();
            }
            else
            {
                QString username = config->value ("test-greeter-config/username").toString ();
                if (username != "")
                {
                    notify_status ("GREETER AUTHENTICATE USERNAME=%s", username.toAscii ().constData ());
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
