#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <glib-object.h>
#include <xcb/xcb.h>
#include <QLightDM/Greeter>
#include <QLightDM/Power>
#include <QLightDM/UsersModel>
#include <QLightDM/SessionsModel>
#include <QtCore/QSettings>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>
#include <QStringList>

#include "test-qt-greeter.h"
#include "status.h"

static gchar *greeter_id;
static QCoreApplication *app = NULL;
static QSettings *config = NULL;
static QLightDM::PowerInterface *power = NULL;
static TestGreeter *greeter = NULL;
static QLightDM::UsersModel *users_model = NULL;
static QLightDM::SessionsModel *sessions_model = NULL;

TestGreeter::TestGreeter ()
{
    connect (this, SIGNAL(showMessage(QString, QLightDM::Greeter::MessageType)), SLOT(showMessage(QString, QLightDM::Greeter::MessageType)));
    connect (this, SIGNAL(showPrompt(QString, QLightDM::Greeter::PromptType)), SLOT(showPrompt(QString, QLightDM::Greeter::PromptType)));
    connect (this, SIGNAL(authenticationComplete()), SLOT(authenticationComplete()));
    connect (this, SIGNAL(autologinTimerExpired()), SLOT(autologinTimerExpired()));
}

void TestGreeter::showMessage (QString text, QLightDM::Greeter::MessageType type)
{
    status_notify ("%s SHOW-MESSAGE TEXT=\"%s\"", greeter_id, text.toLatin1 ().constData ());
}

void TestGreeter::showPrompt (QString text, QLightDM::Greeter::PromptType type)
{
    status_notify ("%s SHOW-PROMPT TEXT=\"%s\"", greeter_id, text.toLatin1 ().constData ());
}

void TestGreeter::authenticationComplete ()
{
    if (authenticationUser () != "")
        status_notify ("%s AUTHENTICATION-COMPLETE USERNAME=%s AUTHENTICATED=%s",
                       greeter_id,
                       authenticationUser ().toLatin1 ().constData (), isAuthenticated () ? "TRUE" : "FALSE");
    else
        status_notify ("%s AUTHENTICATION-COMPLETE AUTHENTICATED=%s", greeter_id, isAuthenticated () ? "TRUE" : "FALSE");
}

void TestGreeter::autologinTimerExpired ()
{
}

void TestGreeter::printHints ()
{
    if (selectUserHint() != "")
        status_notify ("%s SELECT-USER-HINT USERNAME=%s", greeter_id, greeter->selectUserHint ().toLatin1 ().constData ());
    if (selectGuestHint())
        status_notify ("%s SELECT-GUEST-HINT", greeter_id);
    if (lockHint())
        status_notify ("%s LOCK-HINT", greeter_id);
    if (!hasGuestAccountHint ())
        status_notify ("%s HAS-GUEST-ACCOUNT-HINT=FALSE", greeter_id);
    if (hideUsersHint ())
        status_notify ("%s HIDE-USERS-HINT", greeter_id);
    if (showManualLoginHint ())
        status_notify ("%s SHOW-MANUAL-LOGIN-HINT", greeter_id);
    if (!showRemoteLoginHint ())
        status_notify ("%s SHOW-REMOTE-LOGIN-HINT=FALSE", greeter_id);
    if (autologinUserHint () != "")
        status_notify ("%s AUTOLOGIN-USER-HINT=%s", greeter_id, autologinUserHint ().toLatin1 ().constData ());
    if (autologinGuestHint ())
        status_notify ("%s AUTOLOGIN-GUEST-HINT", greeter_id);
    if (autologinSessionHint () != "")
        status_notify ("%s AUTOLOGIN-SESSION-HINT=%s", greeter_id, autologinSessionHint ().toLatin1 ().constData ());
    if (autologinTimeoutHint () != 0)
        status_notify ("%s AUTOLOGIN-TIMEOUT-HINT=%d", greeter_id, autologinTimeoutHint ());
}

void TestGreeter::idle ()
{
    status_notify ("%s IDLE", greeter_id);
}

void TestGreeter::reset ()
{
    status_notify ("%s RESET", greeter_id);
    printHints ();
}

void TestGreeter::userRowsInserted (const QModelIndex & parent, int start, int end)
{
    for (int i = start; i <= end; i++)
    {
        QString name = users_model->data (users_model->index (i, 0), QLightDM::UsersModel::NameRole).toString ();
        status_notify ("%s USER-ADDED USERNAME=%s", greeter_id, qPrintable (name));
    }
}

void TestGreeter::userRowsRemoved (const QModelIndex & parent, int start, int end)
{
    for (int i = start; i <= end; i++)
    {
        QString name = users_model->data (users_model->index (i, 0), QLightDM::UsersModel::NameRole).toString ();
        status_notify ("%s USER-REMOVED USERNAME=%s", greeter_id, qPrintable (name));
    }
}

static void
signal_cb (int signum)
{
    status_notify ("%s TERMINATE SIGNAL=%d", greeter_id, signum);
    _exit (EXIT_SUCCESS);
}

static void
request_cb (const gchar *name, GHashTable *params)
{
    gchar *r;

    if (!name)
    {
        app->quit ();
        return;
    }
  
    if (strcmp (name, "AUTHENTICATE") == 0)
    {
        if (g_hash_table_lookup (params, "USERNAME"))
            greeter->authenticate ((const gchar *) g_hash_table_lookup (params, "USERNAME"));
        else
            greeter->authenticate ();
    }

    else if (strcmp (name, "AUTHENTICATE-GUEST") == 0)
        greeter->authenticateAsGuest ();
  
    else if (strcmp (name, "AUTHENTICATE-AUTOLOGIN") == 0)
        greeter->authenticateAutologin ();

    else if (strcmp (name, "AUTHENTICATE-REMOTE") == 0)
        greeter->authenticateRemote ((const gchar *) g_hash_table_lookup (params, "SESSION"), NULL);

    else if (strcmp (name, "RESPOND") == 0)
        greeter->respond ((const gchar *) g_hash_table_lookup (params, "TEXT"));

    else if (strcmp (name, "CANCEL-AUTHENTICATION") == 0)
        greeter->cancelAuthentication ();

    else if (strcmp (name, "START-SESSION") == 0)
    {
        if (g_hash_table_lookup (params, "SESSION"))
        {
            if (!greeter->startSessionSync ((const gchar *) g_hash_table_lookup (params, "SESSION")))
                status_notify ("%s SESSION-FAILED ERROR=%s", greeter_id, "FIXME: Exceptions in Qt");
        }
        else
        {
            if (!greeter->startSessionSync ())
                status_notify ("%s SESSION-FAILED ERROR=%s", greeter_id, "FIXME: Exceptions in Qt");
        }
    }

    else if (strcmp (name, "LOG-USER-LIST-LENGTH") == 0)
        status_notify ("%s LOG-USER-LIST-LENGTH N=%d", greeter_id, users_model->rowCount (QModelIndex ()));

    else if (strcmp (name, "LOG-USER") == 0)
    {
        const gchar *username = (const gchar *) g_hash_table_lookup (params, "USERNAME");
        for (int i = 0; i < users_model->rowCount (QModelIndex ()); i++)
        {
            QString name = users_model->data (users_model->index (i, 0), QLightDM::UsersModel::NameRole).toString ();
            if (name == username)
                status_notify ("%s LOG-USER USERNAME=%s", greeter_id, qPrintable (name));
        }
    }

    else if (strcmp (name, "LOG-USER-LIST") == 0)
    {
        for (int i = 0; i < users_model->rowCount (QModelIndex ()); i++)
        {
            QString name = users_model->data (users_model->index (i, 0), QLightDM::UsersModel::NameRole).toString ();
            status_notify ("%s LOG-USER USERNAME=%s", greeter_id, qPrintable (name));
        }
    }

    else if (strcmp (name, "LOG-SESSIONS") == 0)
    {
        QStringList names;
        for (int i = 0; i < sessions_model->rowCount (QModelIndex ()); i++)
	    names.append (sessions_model->data (sessions_model->index (i, 0), QLightDM::SessionsModel::KeyRole).toString ());
        names.sort ();
        for (int i = 0; i < names.size (); i++)
            status_notify ("%s LOG-SESSION KEY=%s", greeter_id, qPrintable (names.at (i)));
    }

    else if (strcmp (name, "GET-CAN-SUSPEND") == 0)
    {
        gboolean can_suspend = power->canSuspend ();
        status_notify ("%s CAN-SUSPEND ALLOWED=%s", greeter_id, can_suspend ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "SUSPEND") == 0)
    {
        if (!power->suspend ())
            status_notify ("%s FAIL-SUSPEND", greeter_id);
    }

    else if (strcmp (name, "GET-CAN-HIBERNATE") == 0)
    {
        gboolean can_hibernate = power->canHibernate ();
        status_notify ("%s CAN-HIBERNATE ALLOWED=%s", greeter_id, can_hibernate ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "HIBERNATE") == 0)
    {
        if (!power->hibernate ())
            status_notify ("%s FAIL-HIBERNATE", greeter_id);
    }

    else if (strcmp (name, "GET-CAN-RESTART") == 0)
    {
        gboolean can_restart = power->canRestart ();
        status_notify ("%s CAN-RESTART ALLOWED=%s", greeter_id, can_restart ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "RESTART") == 0)
    {
        if (!power->restart ())
            status_notify ("%s FAIL-RESTART", greeter_id);
    }

    else if (strcmp (name, "GET-CAN-SHUTDOWN") == 0)
    {
        gboolean can_shutdown = power->canShutdown ();
        status_notify ("%s CAN-SHUTDOWN ALLOWED=%s", greeter_id, can_shutdown ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "SHUTDOWN") == 0)
    {
        if (!power->shutdown ())
            status_notify ("%s FAIL-SHUTDOWN", greeter_id);
    }
}

int
main(int argc, char *argv[])
{
    gchar *display, *xdg_seat, *xdg_vtnr, *xdg_session_cookie, *xdg_session_class;
    g_autoptr(GString) status_text = NULL;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    display = getenv ("DISPLAY");
    xdg_seat = getenv ("XDG_SEAT");
    xdg_vtnr = getenv ("XDG_VTNR");
    xdg_session_cookie = getenv ("XDG_SESSION_COOKIE");
    xdg_session_class = getenv ("XDG_SESSION_CLASS");
    if (display)
    {
        if (display[0] == ':')
            greeter_id = g_strdup_printf ("GREETER-X-%s", display + 1);
        else
            greeter_id = g_strdup_printf ("GREETER-X-%s", display);
    }
    else
        greeter_id = g_strdup ("GREETER-?");

    status_connect (request_cb, greeter_id);

    /* Workaround for Qt being confused by libsystem */
#if QT_VERSION >= QT_VERSION_CHECK (5, 3, 0)
    QCoreApplication::setSetuidAllowed (true);
#endif  

    app = new QCoreApplication (argc, argv);

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    status_text = g_string_new ("");
    g_string_printf (status_text, "%s START", greeter_id);
    if (xdg_seat)
        g_string_append_printf (status_text, " XDG_SEAT=%s", xdg_seat);
    if (xdg_vtnr)
        g_string_append_printf (status_text, " XDG_VTNR=%s", xdg_vtnr);
    if (xdg_session_cookie)
        g_string_append_printf (status_text, " XDG_SESSION_COOKIE=%s", xdg_session_cookie);
    if (xdg_session_class)
        g_string_append_printf (status_text, " XDG_SESSION_CLASS=%s", xdg_session_class);
    status_notify ("%s", status_text->str);

    config = new QSettings (g_build_filename (getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), QSettings::IniFormat);

    if (display)
    {
        xcb_connection_t *connection = xcb_connect (NULL, NULL);
        if (xcb_connection_has_error (connection))
        {
            status_notify ("%s FAIL-CONNECT-XSERVER", greeter_id);
            return EXIT_FAILURE;
        }
        status_notify ("%s CONNECT-XSERVER", greeter_id);
    }

    power = new QLightDM::PowerInterface();

    greeter = new TestGreeter();
    if (config->value ("test-greeter-config/resettable", "false") == "true")
    {
        greeter->setResettable (true);
        QObject::connect (greeter, SIGNAL(idle()), greeter, SLOT(idle()));
        QObject::connect (greeter, SIGNAL(reset()), greeter, SLOT(reset()));
    }

    users_model = new QLightDM::UsersModel();
    if (config->value ("test-greeter-config/log-user-changes", "false") == "true")
    {
        QObject::connect (users_model, SIGNAL(rowsInserted(const QModelIndex&, int, int)), greeter, SLOT(userRowsInserted(const QModelIndex&, int, int)));
        QObject::connect (users_model, SIGNAL(rowsAboutToBeRemoved(const QModelIndex&, int, int)), greeter, SLOT(userRowsRemoved(const QModelIndex&, int, int)));
    }

    sessions_model = new QLightDM::SessionsModel();

    status_notify ("%s CONNECT-TO-DAEMON", greeter_id);
    if (!greeter->connectSync())
    {
        status_notify ("%s FAIL-CONNECT-DAEMON", greeter_id);
        return EXIT_FAILURE;
    }

    status_notify ("%s CONNECTED-TO-DAEMON", greeter_id);

    greeter->printHints();

    return app->exec();
}
