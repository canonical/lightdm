#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <glib-object.h>
#include <xcb/xcb.h>
#include <QLightDM/Greeter>
#include <QtCore/QSettings>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>

#include "test-qt-greeter.h"
#include "status.h"

static QCoreApplication *app = NULL;
static QSettings *config = NULL;
static TestGreeter *greeter = NULL;

TestGreeter::TestGreeter ()
{
    connect (this, SIGNAL(showMessage(QString, QLightDM::Greeter::MessageType)), SLOT(showMessage(QString, QLightDM::Greeter::MessageType)));
    connect (this, SIGNAL(showPrompt(QString, QLightDM::Greeter::PromptType)), SLOT(showPrompt(QString, QLightDM::Greeter::PromptType)));
    connect (this, SIGNAL(authenticationComplete()), SLOT(authenticationComplete()));
    connect (this, SIGNAL(autologinTimerExpired()), SLOT(autologinTimerExpired()));
}

void TestGreeter::showMessage (QString text, QLightDM::Greeter::MessageType type)
{
    status_notify ("GREETER %s SHOW-MESSAGE TEXT=\"%s\"", getenv ("DISPLAY"), text.toAscii ().constData ());
}

void TestGreeter::showPrompt (QString text, QLightDM::Greeter::PromptType type)
{
    status_notify ("GREETER %s SHOW-PROMPT TEXT=\"%s\"", getenv ("DISPLAY"), text.toAscii ().constData ());
}

void TestGreeter::authenticationComplete ()
{
    if (authenticationUser () != "")
        status_notify ("GREETER %s AUTHENTICATION-COMPLETE USERNAME=%s AUTHENTICATED=%s",
                       getenv ("DISPLAY"),
                       authenticationUser ().toAscii ().constData (), isAuthenticated () ? "TRUE" : "FALSE");
    else
        status_notify ("GREETER %s AUTHENTICATION-COMPLETE AUTHENTICATED=%s", getenv ("DISPLAY"), isAuthenticated () ? "TRUE" : "FALSE");
}

void TestGreeter::autologinTimerExpired ()
{
    status_notify ("GREETER %s AUTOLOGIN-TIMER-EXPIRED", getenv ("DISPLAY"));
}

static void
signal_cb (int signum)
{
    status_notify ("GREETER %s TERMINATE SIGNAL=%d", getenv ("DISPLAY"), signum);
    _exit (EXIT_SUCCESS);
}

static void
request_cb (const gchar *request)
{
    gchar *r;

    if (!request)
    {
        app->quit ();
        return;
    }
  
    r = g_strdup_printf ("GREETER %s AUTHENTICATE", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
        greeter->authenticate ();
    g_free (r);

    r = g_strdup_printf ("GREETER %s AUTHENTICATE USERNAME=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
        greeter->authenticate (request + strlen (r));
    g_free (r);

    r = g_strdup_printf ("GREETER %s AUTHENTICATE-GUEST", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
        greeter->authenticateAsGuest ();
    g_free (r);

    r = g_strdup_printf ("GREETER %s AUTHENTICATE-AUTOLOGIN", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
        greeter->authenticateAutologin ();
    g_free (r);

    r = g_strdup_printf ("GREETER %s AUTHENTICATE-REMOTE SESSION=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
        greeter->authenticateRemote (request + strlen (r), NULL);
    g_free (r);

    r = g_strdup_printf ("GREETER %s RESPOND TEXT=\"", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
    {
        gchar *text = g_strdup (request + strlen (r));
        text[strlen (text) - 1] = '\0';
        greeter->respond (text);
        g_free (text);
    }
    g_free (r);

    r = g_strdup_printf ("GREETER %s START-SESSION", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
    {
        if (!greeter->startSessionSync ())
            status_notify ("GREETER %s SESSION-FAILED", getenv ("DISPLAY"));
    }
    g_free (r);

    r = g_strdup_printf ("GREETER %s START-SESSION SESSION=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
    {
        if (!greeter->startSessionSync (request + strlen (r)))
            status_notify ("GREETER %s SESSION-FAILED", getenv ("DISPLAY"));
    }
    g_free (r);
}

int
main(int argc, char *argv[])
{
#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    status_connect (request_cb);

    app = new QCoreApplication (argc, argv);

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    status_notify ("GREETER %s START", getenv ("DISPLAY"));

    config = new QSettings (g_build_filename (getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), QSettings::IniFormat);

    xcb_connection_t *connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        status_notify ("GREETER %s FAIL-CONNECT-XSERVER", getenv ("DISPLAY"));
        return EXIT_FAILURE;
    }

    status_notify ("GREETER %s CONNECT-XSERVER", getenv ("DISPLAY"));

    greeter = new TestGreeter();
  
    status_notify ("GREETER %s CONNECT-TO-DAEMON", getenv ("DISPLAY"));
    if (!greeter->connectSync())
    {
        status_notify ("GREETER %s FAIL-CONNECT-DAEMON", getenv ("DISPLAY"));
        return EXIT_FAILURE;
    }

    status_notify ("GREETER %s CONNECTED-TO-DAEMON", getenv ("DISPLAY"));

    if (greeter->selectUserHint() != "")
        status_notify ("GREETER %s SELECT-USER-HINT USERNAME=%s", getenv ("DISPLAY"), greeter->selectUserHint ().toAscii ().constData ());
    if (greeter->lockHint())
        status_notify ("GREETER %s LOCK-HINT", getenv ("DISPLAY"));

    return app->exec();
}
