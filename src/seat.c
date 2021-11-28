/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "seat.h"
#include "configuration.h"
#include "guest-account.h"
#include "greeter-session.h"
#include "session-config.h"

enum {
    SESSION_ADDED,
    RUNNING_USER_SESSION,
    SESSION_REMOVED,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    /* XDG name for this seat */
    gchar *name;

    /* Configuration for this seat */
    GHashTable *properties;

    /* TRUE if this seat can run multiple sessions at once */
    gboolean supports_multi_session;

    /* TRUE if display server can be shared for sessions */
    gboolean share_display_server;

    /* The display servers on this seat */
    GList *display_servers;

    /* The sessions on this seat */
    GList *sessions;

    /* The last session set to active */
    Session *active_session;

    /* The session belonging to the active greeter user */
    Session *next_session;

    /* The session to set active when it starts */
    Session *session_to_activate;

    /* TRUE once we have started */
    gboolean started;

    /* TRUE if stopping this seat (waiting for displays to stop) */
    gboolean stopping;

    /* TRUE if stopped */
    gboolean stopped;

    /* The greeter to be started to replace the current one */
    GreeterSession *replacement_greeter;
} SeatPrivate;

static void seat_logger_iface_init (LoggerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (Seat, seat, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (Seat)
                         G_IMPLEMENT_INTERFACE (
                             LOGGER_TYPE, seat_logger_iface_init))

typedef struct
{
    gchar *name;
    GType type;
} SeatModule;
static GHashTable *seat_modules = NULL;

// FIXME: Make a get_display_server() that re-uses display servers if supported
static DisplayServer *create_display_server (Seat *seat, Session *session);
static gboolean start_display_server (Seat *seat, DisplayServer *display_server);
static GreeterSession *create_greeter_session (Seat *seat);
static void start_session (Seat *seat, Session *session);

static void
free_seat_module (gpointer data)
{
    SeatModule *module = data;
    g_free (module->name);
    g_free (module);
}

void seat_register_module(const gchar *name, GType type)
{
    if (!seat_modules)
        seat_modules = g_hash_table_new_full (g_str_hash, g_str_equal, free_seat_module, NULL);

    g_debug ("Registered seat module %s", name);

    SeatModule *module = g_malloc0 (sizeof (SeatModule));
    module->name = g_strdup (name);
    module->type = type;
    g_hash_table_insert (seat_modules, g_strdup (name), module);
}

Seat *
seat_new (const gchar *module_name)
{
    g_return_val_if_fail (module_name != NULL, NULL);

    SeatModule *m = NULL;
    if (seat_modules)
        m = g_hash_table_lookup (seat_modules, module_name);
    if (!m)
        return NULL;

    return g_object_new (m->type, NULL);
}

void
seat_set_name (Seat *seat, const gchar *name)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    g_return_if_fail (seat != NULL);
    g_free (priv->name);
    priv->name = g_strdup (name);
}

void seat_set_property(Seat *seat, const gchar *name, const gchar *value)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_if_fail (seat != NULL);
    g_hash_table_insert (priv->properties, g_strdup (name), g_strdup (value));
}

const gchar *
seat_get_string_property (Seat *seat, const gchar *name)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, NULL);
    return g_hash_table_lookup (priv->properties, name);
}

gchar **
seat_get_string_list_property (Seat *seat, const gchar *name)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, NULL);
    return g_strsplit (g_hash_table_lookup (priv->properties, name), ";", 0);
}

gboolean
seat_get_boolean_property (Seat *seat, const gchar *name)
{
    const gchar *value = seat_get_string_property (seat, name);
    if (!value)
        return FALSE;

    /* Count the number of non-whitespace characters */
    gint length = 0;
    for (gint i = 0; value[i]; i++)
        if (!g_ascii_isspace (value[i]))
            length = i + 1;

    return strncmp (value, "true", MAX (length, 4)) == 0;
}

gint
seat_get_integer_property (Seat *seat, const gchar *name)
{
    const gchar *value = seat_get_string_property (seat, name);
    return value ? atoi (value) : 0;
}

const gchar *
seat_get_name (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, NULL);
    return priv->name;
}

void
seat_set_supports_multi_session (Seat *seat, gboolean supports_multi_session)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_if_fail (seat != NULL);
    priv->supports_multi_session = supports_multi_session;
}

void
seat_set_share_display_server (Seat *seat, gboolean share_display_server)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_if_fail (seat != NULL);
    priv->share_display_server = share_display_server;
}

/**
 * @brief seat_start
 * 获取Seat关联的 SeatClass 对象的setup成员变量和start成员变量。
 * 这2个成员变量都是函数指针，setup指向的方法是 seat_local_setup，此函数什么事都没做。
 * start指向了 seat_local_start 函数首先读取自动登录配置，如果没有自动登录或者登录失败则启动greeter会话。
 * @param Seat*
 */
gboolean seat_start(Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    g_return_val_if_fail (seat != NULL, FALSE);

    l_debug (seat, "Starting");

    SEAT_GET_CLASS (seat)->setup (seat);
    priv->started = SEAT_GET_CLASS (seat)->start (seat);

    return priv->started;
}

GList *
seat_get_sessions (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, NULL);
    return priv->sessions;
}

static gboolean
set_greeter_idle (gpointer greeter)
{
    greeter_idle (GREETER (greeter));
    return FALSE;
}

void
seat_set_active_session (Seat *seat, Session *session)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    g_return_if_fail (seat != NULL);

    SEAT_GET_CLASS (seat)->set_active_session (seat, session);

    /* Stop any greeters */
    for (GList *link = priv->sessions; link; link = link->next)
    {
        Session *s = link->data;

        if (s == session || session_get_is_stopping (s))
            continue;

        if (IS_GREETER_SESSION (s))
        {
            Greeter *greeter = greeter_session_get_greeter (GREETER_SESSION (s));
            if (greeter_get_resettable (greeter))
            {
                if (priv->active_session == s)
                {
                    l_debug (seat, "Idling greeter");
                    /* Do this in an idle callback, because we might very well
                       be in the middle of responding to a START_SESSION
                       request by a greeter.  So they won't expect an IDLE
                       call during that.  Plus, this isn't time-sensitive. */
                    g_idle_add (set_greeter_idle, greeter);
                }
            }
            else
            {
                l_debug (seat, "Stopping greeter");
                session_stop (s);
            }
        }
    }

    /* Lock previous sessions */
    if (priv->active_session && session != priv->active_session && !IS_GREETER_SESSION (priv->active_session))
        session_lock (priv->active_session);

    session_activate (session);
    g_clear_object (&priv->active_session);
    priv->active_session = g_object_ref (session);
}

Session *
seat_get_active_session (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return SEAT_GET_CLASS (seat)->get_active_session (seat);
}

Session *
seat_get_next_session (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, NULL);
    return priv->next_session;
}

/**
 * Obtains the active session which lightdm expects to be active.
 *
 * This function is different from seat_get_active_session() in that the
 * later (in the case of local seats) dynamically finds the session that is
 * really active (based on the active VT), whereas this function returns the
 * session that lightdm activated last by itself, which may not be the actual
 * active session (i.e. VT changes).
 */
Session *
seat_get_expected_active_session (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, NULL);
    return priv->active_session;
}

/**
 * Sets the active session which lightdm expects to be active.
 *
 * This function is different from seat_set_active_session() in that the
 * later performs an actual session activation, whereas this function just
 * updates the active session after the session has been activated by some
 * means external to lightdm (i.e. VT changes).
 */
void
seat_set_externally_activated_session (Seat *seat, Session *session)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_if_fail (seat != NULL);
    g_clear_object (&priv->active_session);
    priv->active_session = g_object_ref (session);
}

Session *
seat_find_session_by_login1_id (Seat *seat, const gchar *login1_session_id)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    for (GList *session_link = priv->sessions; session_link; session_link = session_link->next)
    {
        Session *session = session_link->data;
        if (g_strcmp0 (login1_session_id, session_get_login1_session_id (session)) == 0)
            return session;
    }

    return NULL;
}

gboolean
seat_get_can_switch (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, FALSE);
    return seat_get_boolean_property (seat, "allow-user-switching") && priv->supports_multi_session;
}

gboolean
seat_get_allow_guest (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return seat_get_boolean_property (seat, "allow-guest") && guest_account_is_installed ();
}

static gboolean
run_script (Seat *seat, DisplayServer *display_server, const gchar *script_name, User *user)
{
    g_autoptr(Process) script = process_new (NULL, NULL);

    process_set_command (script, script_name);

    /* Set POSIX variables */
    process_set_clear_environment (script, TRUE);
    process_set_env (script, "SHELL", "/bin/sh");

    if (g_getenv ("LD_PRELOAD"))
        process_set_env (script, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
    if (g_getenv ("LD_LIBRARY_PATH"))
        process_set_env (script, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    if (g_getenv ("PATH"))
        process_set_env (script, "PATH", g_getenv ("PATH"));

    /* Variables required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
        process_set_env (script, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));

    process_set_env (script, "XDG_SEAT", seat_get_name (seat));

    if (user)
    {
        process_set_env (script, "USER", user_get_name (user));
        process_set_env (script, "LOGNAME", user_get_name (user));
        process_set_env (script, "HOME", user_get_home_directory (user));
    }
    else
        process_set_env (script, "HOME", "/");

    SEAT_GET_CLASS (seat)->run_script (seat, display_server, script);

    gboolean result = FALSE;
    if (process_start (script, TRUE))
    {
        int exit_status = process_get_exit_status (script);
        if (WIFEXITED (exit_status))
        {
            l_debug (seat, "Exit status of %s: %d", script_name, WEXITSTATUS (exit_status));
            result = WEXITSTATUS (exit_status) == EXIT_SUCCESS;
        }
    }

    return result;
}

static void
seat_real_run_script (Seat *seat, DisplayServer *display_server, Process *process)
{
}

static void
emit_upstart_signal (const gchar *signal)
{
    g_return_if_fail (signal != NULL);
    g_return_if_fail (signal[0] != 0);

    if (getuid () != 0)
        return;

    /* OK if it fails, probably not installed or not running upstart */
    g_autoptr(GSubprocess) p = g_subprocess_new (G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL, "initctl", "-q", "emit", signal, "DISPLAY_MANAGER=lightdm", NULL);
}

static void
check_stopped (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    if (priv->stopping &&
        !priv->stopped &&
        g_list_length (priv->display_servers) == 0 &&
        g_list_length (priv->sessions) == 0)
    {
        priv->stopped = TRUE;
        l_debug (seat, "Stopped");
        g_signal_emit (seat, signals[STOPPED], 0);
    }
}

static void
display_server_stopped_cb (DisplayServer *display_server, Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    l_debug (seat, "Display server stopped");

    /* Run a script right after stopping the display server */
    const gchar *script = seat_get_string_property (seat, "display-stopped-script");
    if (script)
        run_script (seat, NULL, script, NULL);

    g_signal_handlers_disconnect_matched (display_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    priv->display_servers = g_list_remove (priv->display_servers, display_server);

    if (priv->stopping || !priv->started)
    {
        check_stopped (seat);
        g_object_unref (display_server);
        return;
    }

    /* Stop all sessions on this display server */
    GList *list = g_list_copy (priv->sessions);
    for (GList *link = list; link; link = link->next)
        g_object_ref (link->data);
    for (GList *link = list; link; link = link->next)
    {
        Session *session = link->data;

        if (session_get_display_server (session) != display_server || session_get_is_stopping (session))
            continue;

        gboolean is_failed_greeter = IS_GREETER_SESSION (session) && !session_get_is_started (session);

        l_debug (seat, "Stopping session");
        session_stop (session);

        /* Stop seat if this is the only display server and it failed to start a greeter */
        if (is_failed_greeter &&
            g_list_length (priv->display_servers) == 0)
        {
            l_debug (seat, "Stopping; greeter display server failed to start");
            seat_stop (seat);
        }
    }
    g_list_free_full (list, g_object_unref);

    if (!priv->stopping)
    {
        /* If we were the active session, switch to a greeter */
        Session *active_session = seat_get_active_session (seat);
        if (!active_session || session_get_display_server (active_session) == display_server)
        {
            l_debug (seat, "Active display server stopped, starting greeter");
            if (!seat_switch_to_greeter (seat))
            {
                l_debug (seat, "Stopping; failed to start a greeter");
                seat_stop (seat);
            }
        }
    }

    g_object_unref (display_server);
}

static gboolean
can_share_display_server (Seat *seat, DisplayServer *display_server)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    return priv->share_display_server && display_server_get_can_share (display_server);
}

static GreeterSession *
find_greeter_session (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    for (GList *link = priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        if (!session_get_is_stopping (session) && IS_GREETER_SESSION (session))
            return GREETER_SESSION (session);
    }

    return NULL;
}

static GreeterSession *
find_resettable_greeter (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    for (GList *link = priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        if (!session_get_is_stopping (session) && IS_GREETER_SESSION (session) &&
            greeter_get_resettable (greeter_session_get_greeter (GREETER_SESSION (session))))
            return GREETER_SESSION (session);
    }

    return NULL;
}

static void
set_greeter_hints (Seat *seat, Greeter *greeter)
{
    greeter_clear_hints (greeter);
    greeter_set_hint (greeter, "default-session", seat_get_string_property (seat, "user-session"));
    greeter_set_hint (greeter, "hide-users", seat_get_boolean_property (seat, "greeter-hide-users") ? "true" : "false");
    greeter_set_hint (greeter, "show-manual-login", seat_get_boolean_property (seat, "greeter-show-manual-login") ? "true" : "false");
    greeter_set_hint (greeter, "show-remote-login", seat_get_boolean_property (seat, "greeter-show-remote-login") ? "true" : "false");
    greeter_set_hint (greeter, "has-guest-account", seat_get_allow_guest (seat) && seat_get_boolean_property (seat, "greeter-allow-guest") ? "true" : "false");
}

static void
switch_to_greeter_from_failed_session (Seat *seat, Session *session)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    /* Switch to greeter if one open */
    GreeterSession *greeter_session = find_resettable_greeter (seat);
    gboolean existing = FALSE;
    if (greeter_session)
    {
        l_debug (seat, "Switching to existing greeter");
        set_greeter_hints (seat, greeter_session_get_greeter (greeter_session));
        existing = TRUE;
    }
    else
    {
        greeter_session = create_greeter_session (seat);
    }
    Greeter *greeter = greeter_session_get_greeter (greeter_session);

    if (session_get_is_guest (session))
        greeter_set_hint (greeter, "select-guest", "true");
    else
        greeter_set_hint (greeter, "select-user", session_get_username (session));

    if (existing)
    {
        greeter_reset (greeter);
        seat_set_active_session (seat, SESSION (greeter_session));
    }
    else
    {
        g_clear_object (&priv->session_to_activate);
        priv->session_to_activate = g_object_ref (SESSION (greeter_session));

        if (can_share_display_server (seat, session_get_display_server (session)))
            session_set_display_server (SESSION (greeter_session), session_get_display_server (session));
        else
        {
            DisplayServer *display_server = create_display_server (seat, session);
            session_set_display_server (session, display_server);
            if (!start_display_server (seat, display_server))
            {
                l_debug (seat, "Failed to start display server for greeter");
                seat_stop (seat);
            }
        }

        start_session (seat, SESSION (greeter_session));
    }

    /* Stop failed session */
    session_stop (session);
}

static void
start_session (Seat *seat, Session *session)
{
    /* Use system location for greeter log file */
    if (IS_GREETER_SESSION (session))
    {
        g_autofree gchar *log_dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
        g_autofree gchar *filename = g_strdup_printf ("%s-greeter.log", seat_get_name (seat));
        g_autofree gchar *log_filename = g_build_filename (log_dir, filename, NULL);
        gboolean backup_logs = config_get_boolean (config_get_instance (), "LightDM", "backup-logs");
        session_set_log_file (session, log_filename, backup_logs ? LOG_MODE_BACKUP_AND_TRUNCATE : LOG_MODE_APPEND);
    }

    if (session_start (session))
        return;

    if (IS_GREETER_SESSION (session))
    {
        l_debug (seat, "Failed to start greeter");
        display_server_stop (session_get_display_server (session));
        return;
    }

    l_debug (seat, "Failed to start session, starting greeter");
    switch_to_greeter_from_failed_session (seat, session);
}

static void
run_session (Seat *seat, Session *session)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    const gchar *script;
    if (IS_GREETER_SESSION (session))
        script = seat_get_string_property (seat, "greeter-setup-script");
    else
        script = seat_get_string_property (seat, "session-setup-script");
    if (script && !run_script (seat, session_get_display_server (session), script, session_get_user (session)))
    {
        l_debug (seat, "Switching to greeter due to failed setup script");
        switch_to_greeter_from_failed_session (seat, session);
        return;
    }

    if (!IS_GREETER_SESSION (session))
    {
        g_signal_emit (seat, signals[RUNNING_USER_SESSION], 0, session);
        emit_upstart_signal ("desktop-session-start");
    }

    session_run (session);

    // FIXME: Wait until the session is ready

    if (session == priv->session_to_activate)
    {
        seat_set_active_session (seat, session);
        g_clear_object (&priv->session_to_activate);
    }
    else if (priv->active_session)
    {
        /* Multiple sessions can theoretically be on the same VT (especially
           if using Mir).  If a new session appears on an existing active VT,
           logind will mark it as active, while ConsoleKit will re-mark the
           oldest session as active.  In either case, that may not be the
           session that we want to be active.  So let's be explicit and
           re-activate the correct session whenever a new session starts.
           There's no harm to do this in seats that enforce separate VTs. */
        session_activate (priv->active_session);
    }
}

static Session *
find_user_session (Seat *seat, const gchar *username, Session *ignore_session)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    if (!username)
        return NULL;

    for (GList *link = priv->sessions; link; link = link->next)
    {
        Session *session = link->data;

        if (session == ignore_session)
            continue;

        if (!session_get_is_stopping (session) && g_strcmp0 (session_get_username (session), username) == 0)
            return session;
    }

    return NULL;
}

static void
greeter_active_username_changed_cb (Greeter *greeter, GParamSpec *pspec, Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    Session *session = find_user_session (seat, greeter_get_active_username (greeter), priv->active_session);

    g_clear_object (&priv->next_session);
    priv->next_session = session ? g_object_ref (session) : NULL;

    SEAT_GET_CLASS (seat)->set_next_session (seat, session);
}

static void
session_authentication_complete_cb (Session *session, Seat *seat)
{
    if (session_get_is_authenticated (session))
    {
        Session *s = find_user_session (seat, session_get_username (session), session);
        if (s)
        {
            l_debug (seat, "Session authenticated, switching to existing user session");
            seat_set_active_session (seat, s);
            session_stop (session);
        }
        else
        {
            l_debug (seat, "Session authenticated, running command");
            run_session (seat, session);
        }
    }
    else if (!IS_GREETER_SESSION (session))
    {
        l_debug (seat, "Switching to greeter due to failed authentication");
        switch_to_greeter_from_failed_session (seat, session);
    }
    else
    {
        l_debug (seat, "Stopping session that failed authentication");
        session_stop (session);
    }
}

static void
session_stopped_cb (Session *session, Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    l_debug (seat, "Session stopped");

    g_signal_handlers_disconnect_matched (session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    priv->sessions = g_list_remove (priv->sessions, session);
    if (session == priv->active_session)
        g_clear_object (&priv->active_session);
    if (session == priv->next_session)
        g_clear_object (&priv->next_session);
    if (session == priv->session_to_activate)
        g_clear_object (&priv->session_to_activate);

    DisplayServer *display_server = session_get_display_server (session);

    /* Cleanup */
    if (!IS_GREETER_SESSION (session))
    {
        const gchar *script = seat_get_string_property (seat, "session-cleanup-script");
        if (script)
            run_script (seat, display_server, script, session_get_user (session));
    }

    /* We were waiting for this session, but it didn't start :( */
    // FIXME: Start a greeter on this?
    if (session == priv->session_to_activate)
        g_clear_object (&priv->session_to_activate);

    if (priv->stopping)
    {
        check_stopped (seat);
        g_object_unref (session);
        return;
    }

    /* If there is a pending replacement greeter, start it */
    if (IS_GREETER_SESSION (session) && priv->replacement_greeter)
    {
        GreeterSession *replacement_greeter = priv->replacement_greeter;
        priv->replacement_greeter = NULL;

        if (session_get_is_authenticated (SESSION (replacement_greeter)))
        {
            l_debug (seat, "Greeter stopped, running session");
            run_session (seat, SESSION (replacement_greeter));
        }
        else
        {
            l_debug (seat, "Greeter stopped, starting session authentication");
            start_session (seat, SESSION (replacement_greeter));
        }

        g_object_unref (replacement_greeter);
    }
    /* If this is the greeter session then re-use this display server */
    else if (IS_GREETER_SESSION (session) &&
        can_share_display_server (seat, display_server) &&
        greeter_get_start_session (greeter_session_get_greeter (GREETER_SESSION (session))))
    {
        for (GList *link = priv->sessions; link; link = link->next)
        {
            Session *s = link->data;

            /* Skip this session and sessions on other display servers */
            if (s == session || session_get_display_server (s) != display_server || session_get_is_stopping (s))
                continue;

            if (session_get_is_authenticated (s))
            {
                l_debug (seat, "Greeter stopped, running session");
                run_session (seat, s);
            }
            else
            {
                l_debug (seat, "Greeter stopped, starting session authentication");
                start_session (seat, s);
            }
            break;
        }
    }
    /* If this is the greeter and nothing else is running then stop the seat */
    else if (IS_GREETER_SESSION (session) &&
        !greeter_get_start_session (greeter_session_get_greeter (GREETER_SESSION (session))) &&
        g_list_length (priv->display_servers) == 1 &&
        g_list_nth_data (priv->display_servers, 0) == display_server)
    {
        l_debug (seat, "Stopping; failed to start a greeter");
        seat_stop (seat);
    }
    /* If we were the active session, switch to a greeter */
    else if (!IS_GREETER_SESSION (session) && session == seat_get_active_session (seat))
    {
        l_debug (seat, "Active session stopped, starting greeter");
        if (!seat_switch_to_greeter (seat))
        {
            l_debug (seat, "Stopping; failed to start a greeter");
            seat_stop (seat);
        }
    }

    g_signal_emit (seat, signals[SESSION_REMOVED], 0, session);
    g_object_unref (session);

    /* Stop the display server if no-longer required */
    if (display_server && !display_server_get_is_stopping (display_server) &&
        !SEAT_GET_CLASS (seat)->display_server_is_used (seat, display_server))
    {
        l_debug (seat, "Stopping display server, no sessions require it");
        display_server_stop (display_server);
    }
}

static void
set_session_env (Session *session)
{
    /* Connect using the session bus */
    if (getuid () != 0)
    {
        if (g_getenv ("DBUS_SESSION_BUS_ADDRESS"))
            session_set_env (session, "DBUS_SESSION_BUS_ADDRESS", g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
        session_set_env (session, "LDM_BUS", "SESSION");
    }

    /* Variables required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
    {
        session_set_env (session, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));
        session_set_env (session, "DBUS_SYSTEM_BUS_ADDRESS", g_getenv ("DBUS_SYSTEM_BUS_ADDRESS"));
        session_set_env (session, "DBUS_SESSION_BUS_ADDRESS", g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
        session_set_env (session, "GI_TYPELIB_PATH", g_getenv ("GI_TYPELIB_PATH"));
    }

    if (g_getenv ("LD_PRELOAD"))
        session_set_env (session, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
    if (g_getenv ("LD_LIBRARY_PATH"))
        session_set_env (session, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
}

static Session *
create_session (Seat *seat, gboolean autostart)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    Session *session = SEAT_GET_CLASS (seat)->create_session (seat);
    priv->sessions = g_list_append (priv->sessions, session);
    if (autostart)
        g_signal_connect (session, SESSION_SIGNAL_AUTHENTICATION_COMPLETE, G_CALLBACK (session_authentication_complete_cb), seat);
    g_signal_connect (session, SESSION_SIGNAL_STOPPED, G_CALLBACK (session_stopped_cb), seat);

    set_session_env (session);

    g_signal_emit (seat, signals[SESSION_ADDED], 0, session);

    return session;
}

static gchar **
get_session_argv (Seat *seat, SessionConfig *session_config, const gchar *session_wrapper)
{
    /* If configured, run sessions through a wrapper */
    if (session_wrapper)
    {
        gchar **argv = g_malloc (sizeof (gchar *) * 3);
        g_autofree gchar *path = g_find_program_in_path (session_wrapper);
        argv[0] = path ? g_steal_pointer (&path) : g_strdup (session_wrapper);
        argv[1] = g_strdup (session_config_get_command (session_config));
        argv[2] = NULL;
        return argv;
    }

    /* Split command into an array listing and make command absolute */
    int argc;
    g_auto(GStrv) argv = NULL;
    g_autoptr(GError) error = NULL;
    gboolean result = g_shell_parse_argv (session_config_get_command (session_config), &argc, &argv, &error);
    if (error)
        l_debug (seat, "Invalid session command '%s': %s", session_config_get_command (session_config), error->message);
    if (!result)
        return NULL;
    g_autofree gchar *path = g_find_program_in_path (argv[0]);
    if (path)
    {
        g_free (argv[0]);
        argv[0] = g_steal_pointer (&path);
    }

    return g_steal_pointer (&argv);
}

static SessionConfig *
find_session_config (Seat *seat, const gchar *sessions_dir, const gchar *session_name)
{
    g_return_val_if_fail (sessions_dir != NULL, NULL);
    g_return_val_if_fail (session_name != NULL, NULL);

    g_auto(GStrv) dirs = g_strsplit (sessions_dir, ":", -1);
    for (int i = 0; dirs[i]; i++)
    {
        const gchar *default_session_type = "x";
        if (dirs[i] != NULL && g_str_has_suffix (dirs[i], "/wayland-sessions") == TRUE)
            default_session_type = "wayland";

        g_autofree gchar *filename = g_strdup_printf ("%s.desktop", session_name);
        g_autofree gchar *path = g_build_filename (dirs[i], filename, NULL);
        g_autoptr(GError) error = NULL;
        SessionConfig *session_config = session_config_new_from_file (path, default_session_type, &error);
        if (session_config)
            return session_config;
    }

    l_debug (seat, "Failed to find session configuration %s", session_name);

    return NULL;
}

static void
configure_session (Session *session, SessionConfig *config, const gchar *session_name, const gchar *language)
{
    session_set_config (session, config);
    session_set_env (session, "XDG_SESSION_DESKTOP", session_name);
    session_set_env (session, "DESKTOP_SESSION", session_name);
    session_set_env (session, "GDMSESSION", session_name);
    gchar **desktop_names = session_config_get_desktop_names (config);
    if (desktop_names)
    {
        g_autofree gchar *value = g_strjoinv (":", desktop_names);
        session_set_env (session, "XDG_CURRENT_DESKTOP", value);
    }
    if (language && language[0] != '\0')
    {
        session_set_env (session, "LANG", language);
        session_set_env (session, "GDM_LANG", language);
    }
}

static Session *
create_user_session (Seat *seat, const gchar *username, gboolean autostart)
{
    l_debug (seat, "Creating user session");

    /* Load user preferences */
    g_autoptr(User) user = accounts_get_user_by_name (username);
    if (!user)
    {
        l_debug (seat, "Can't login unknown user '%s'", username);
        return NULL;
    }
    const gchar *session_name = user_get_xsession (user);
    const gchar *language = user_get_language (user);

    /* Override session for autologin if configured */
    if (autostart)
    {
        const gchar *autologin_session_name = seat_get_string_property (seat, "autologin-session");
        if (autologin_session_name)
            session_name = autologin_session_name;
    }

    if (!session_name)
        session_name = seat_get_string_property (seat, "user-session");
    g_autofree gchar *sessions_dir = config_get_string (config_get_instance (), "LightDM", "sessions-directory");
    g_autoptr(SessionConfig) session_config = find_session_config (seat, sessions_dir, session_name);
    if (!session_config)
    {
        l_debug (seat, "Can't find session '%s'", session_name);
        return NULL;
    }

    g_autoptr(Session) session = create_session (seat, autostart);
    configure_session (session, session_config, session_name, language);
    session_set_username (session, username);
    session_set_do_authenticate (session, TRUE);
    g_auto(GStrv) argv = get_session_argv (seat, session_config, seat_get_string_property (seat, "session-wrapper"));
    session_set_argv (session, argv);

    return g_steal_pointer (&session);
}

static void
prepend_argv (gchar ***argv, const gchar *value)
{
    gchar **old_argv = *argv;
    gchar **new_argv = g_malloc (sizeof (gchar *) * (g_strv_length (*argv) + 2));
    new_argv[0] = g_strdup (value);
    gint i;
    for (i = 0; old_argv[i]; i++)
        new_argv[i + 1] = old_argv[i];
    new_argv[i + 1] = NULL;

    g_free (*argv);
    *argv = new_argv;
}

static Session *
create_guest_session (Seat *seat, const gchar *session_name)
{
    if (!session_name)
        session_name = seat_get_string_property (seat, "guest-session");
    if (!session_name)
        session_name = seat_get_string_property (seat, "user-session");
    g_autofree gchar *sessions_dir = config_get_string (config_get_instance (), "LightDM", "sessions-directory");
    g_autoptr(SessionConfig) session_config = find_session_config (seat, sessions_dir, session_name);
    if (!session_config)
    {
        l_debug (seat, "Can't find session '%s'", session_name);
        return NULL;
    }

    g_autoptr(Session) session = create_session (seat, TRUE);
    configure_session (session, session_config, session_name, NULL);
    session_set_do_authenticate (session, TRUE);
    session_set_is_guest (session, TRUE);
    g_auto(GStrv) argv = get_session_argv (seat, session_config, seat_get_string_property (seat, "session-wrapper"));
    const gchar *guest_wrapper = seat_get_string_property (seat, "guest-wrapper");
    if (guest_wrapper)
    {
        g_autofree gchar *path = g_find_program_in_path (guest_wrapper);
        prepend_argv (&argv, path ? path : guest_wrapper);
    }

    session_set_argv (session, argv);

    return g_steal_pointer (&session);
}

// FIXME: This is inefficient and we already know the greeter session when we set the callbacks...
static Session *
get_greeter_session (Seat *seat, Greeter *greeter)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    /* Stop any greeters */
    for (GList *link = priv->sessions; link; link = link->next)
    {
        Session *session = link->data;

        if (IS_GREETER_SESSION (session) && greeter_session_get_greeter (GREETER_SESSION (session)))
            return session;
    }

    return NULL;
}

static Session *
greeter_create_session_cb (Greeter *greeter, Seat *seat)
{
    Session *greeter_session, *session;

    greeter_session = get_greeter_session (seat, greeter);
    session = create_session (seat, FALSE);
    session_set_config (session, session_get_config (greeter_session));
    session_set_display_server (session, session_get_display_server (greeter_session));

    return g_object_ref (session);
}

static gboolean
greeter_start_session_cb (Greeter *greeter, SessionType type, const gchar *session_name, Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    /* Get the session to use */
    Session *session;
    if (greeter_get_guest_authenticated (greeter))
    {
        session = g_object_ref (create_guest_session (seat, session_name));
        if (!session)
            return FALSE;
        session_set_pam_service (session, seat_get_string_property (seat, "pam-autologin-service"));
    }
    else
    {
        session = greeter_take_authentication_session (greeter);

        /* Get session command to run */
        g_autofree gchar *sessions_dir = NULL;
        switch (type)
        {
        case SESSION_TYPE_LOCAL:
            sessions_dir = config_get_string (config_get_instance (), "LightDM", "sessions-directory");
            break;
        case SESSION_TYPE_REMOTE:
            sessions_dir = config_get_string (config_get_instance (), "LightDM", "remote-sessions-directory");
            break;
        }

        /* Load user preferences */
        const gchar *language = NULL;
        User *user = session_get_user (session);
        if (user)
        {
            const gchar *autologin_username;

            /* Override session for autologin if configured */
            autologin_username = seat_get_string_property (seat, "autologin-user");
            if (!session_name && g_strcmp0 (user_get_name (user), autologin_username) == 0)
                session_name = seat_get_string_property (seat, "autologin-session");

            if (!session_name)
                session_name = user_get_xsession (user);
            language = user_get_language (user);
        }

        if (!session_name)
            session_name = seat_get_string_property (seat, "user-session");
        if (user)
            user_set_xsession (session_get_user (session), session_name);

        g_autoptr(SessionConfig) session_config = find_session_config (seat, sessions_dir, session_name);
        if (!session_config)
        {
            l_debug (seat, "Can't find session '%s'", session_name);
            return FALSE;
        }

        configure_session (session, session_config, session_name, language);
        g_auto(GStrv) argv = get_session_argv (seat, session_config, seat_get_string_property (seat, "session-wrapper"));
        session_set_argv (session, argv);
    }

    /* Switch to this session when it is ready */
    g_clear_object (&priv->session_to_activate);
    priv->session_to_activate = session;

    /* Return to existing session if it is open */
    const gchar *username = session_get_username (session);
    Session *existing_session = find_user_session (seat, username, NULL);
    if (existing_session && session != existing_session)
    {
        l_debug (seat, "Returning to existing user session %s", username);
        session_stop (session);
        session_unlock (existing_session);
        seat_set_active_session (seat, existing_session);
        return TRUE;
    }

    /* If can re-use the display server, stop the greeter first */
    Session *greeter_session = get_greeter_session (seat, greeter);
    if (greeter_session)
    {
        DisplayServer *display_server = session_get_display_server (greeter_session);
        if (display_server &&
            !greeter_get_resettable (greeter) &&
            can_share_display_server (seat, display_server) &&
            strcmp (display_server_get_session_type (display_server), session_get_session_type (session)) == 0)
        {
            l_debug (seat, "Stopping greeter; display server will be re-used for user session");

            /* Run on the same display server after the greeter has stopped */
            session_set_display_server (session, display_server);

            /* Stop the greeter */
            session_stop (greeter_session);

            return TRUE;
        }
    }

    /* Otherwise start a new display server for this session */
    DisplayServer *display_server = create_display_server (seat, session);
    session_set_display_server (session, display_server);
    if (!start_display_server (seat, display_server))
    {
        l_debug (seat, "Failed to start display server for new session");
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief create_greeter_session
 * 函数实现了启动greeter会话，会话就是Session。此Session是GreeterSession类型，关联了GreeterSessionClass类型。
 * 此函数获取seat对象的create_greeter_session成员变量。
 *
 * 特别需要注意的是，SeatClass的成员变量与seat.c定义的create_greeter_session不是同一个东西，它是一个函数指针，但本身是变量，不是函数。
 * 对于LightDM默认的Seat类型，它指向了seat_local_create_greeter_session函数。
 * 这个方法得到了父类型的create_greeter_session成员，实际上调用了seat_real_create_greeter_session方法。接下来又调用了greeter_session_new方法。
 * 这个方法返回了一个GreeterSession类型的对象指针。这就是Session，它的start成员变量也是个函数指针，指向了greeter_session_start方法。
 *
 * seat对象create_greeter_session成员变量指向的函数创建了greeter_session。
 * 此方法监听了greeter_session的SESSION_SIGNAL_AUTHENTICATION_COMPLETE信号。当用户单次登录操作完成时触发此信号。
 * 同时继续监听GREETER_SIGNAL_CREATE_SESSION信号和GREETER_SIGNAL_START_SESSION信号，分别由greeter_create_session_cb和greeter_start_session_cb函数处理。
 */
static GreeterSession *
create_greeter_session (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    l_debug (seat, "Creating greeter session");

    g_autofree gchar *sessions_dir = config_get_string (config_get_instance (), "LightDM", "greeters-directory");
    g_autoptr(SessionConfig) session_config = find_session_config (seat, sessions_dir, seat_get_string_property (seat, "greeter-session"));
    if (!session_config)
        return NULL;

    g_auto(GStrv) argv = get_session_argv (seat, session_config, NULL);
    const gchar *greeter_wrapper = seat_get_string_property (seat, "greeter-wrapper");
    if (greeter_wrapper)
    {
        g_autofree gchar *path = g_find_program_in_path (greeter_wrapper);
        prepend_argv (&argv, path ? path : greeter_wrapper);
    }

    GreeterSession *greeter_session = SEAT_GET_CLASS (seat)->create_greeter_session (seat);
    Greeter *greeter = greeter_session_get_greeter (greeter_session);
    session_set_config (SESSION (greeter_session), session_config);
    priv->sessions = g_list_append (priv->sessions, SESSION (greeter_session));
    g_signal_connect (greeter, GREETER_SIGNAL_ACTIVE_USERNAME_CHANGED, G_CALLBACK (greeter_active_username_changed_cb), seat);
    g_signal_connect (greeter_session, SESSION_SIGNAL_AUTHENTICATION_COMPLETE, G_CALLBACK (session_authentication_complete_cb), seat);
    g_signal_connect (greeter_session, SESSION_SIGNAL_STOPPED, G_CALLBACK (session_stopped_cb), seat);

    set_session_env (SESSION (greeter_session));
    session_set_env (SESSION (greeter_session), "XDG_SESSION_CLASS", "greeter");

    session_set_pam_service (SESSION (greeter_session), seat_get_string_property (seat, "pam-greeter-service"));
    if (getuid () == 0)
    {
        g_autofree gchar *greeter_user = config_get_string (config_get_instance (), "LightDM", "greeter-user");
        session_set_username (SESSION (greeter_session), greeter_user);
    }
    else
    {
        /* In test mode run the greeter as ourself */
        session_set_username (SESSION (greeter_session), user_get_name (accounts_get_current_user ()));
    }
    session_set_argv (SESSION (greeter_session), argv);

    greeter_set_pam_services (greeter,
                              seat_get_string_property (seat, "pam-service"),
                              seat_get_string_property (seat, "pam-autologin-service"));
    g_signal_connect (greeter, GREETER_SIGNAL_CREATE_SESSION, G_CALLBACK (greeter_create_session_cb), seat);
    g_signal_connect (greeter, GREETER_SIGNAL_START_SESSION, G_CALLBACK (greeter_start_session_cb), seat);

    /* Set hints to greeter */
    greeter_set_allow_guest (greeter, seat_get_allow_guest (seat));
    set_greeter_hints (seat, greeter);

    /* Configure for automatic login */
    const gchar *autologin_username = seat_get_string_property (seat, "autologin-user");
    if (g_strcmp0 (autologin_username, "") == 0)
        autologin_username = NULL;
    const gchar *autologin_session = seat_get_string_property (seat, "autologin-session");
    if (g_strcmp0 (autologin_session, "") == 0)
        autologin_session = NULL;
    int autologin_timeout = seat_get_integer_property (seat, "autologin-user-timeout");
    gboolean autologin_guest = seat_get_boolean_property (seat, "autologin-guest");
    if (autologin_timeout > 0)
    {
        g_autofree gchar *value = g_strdup_printf ("%d", autologin_timeout);
        greeter_set_hint (greeter, "autologin-timeout", value);
        if (autologin_username)
            greeter_set_hint (greeter, "autologin-user", autologin_username);
        if (autologin_session)
            greeter_set_hint (greeter, "autologin-session", autologin_session);
        if (autologin_guest)
            greeter_set_hint (greeter, "autologin-guest", "true");
    }

    return greeter_session;
}

static Session *
find_session_for_display_server (Seat *seat, DisplayServer *display_server)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    for (GList *link = priv->sessions; link; link = link->next)
    {
        Session *session = link->data;

        if (session_get_display_server (session) == display_server &&
            !session_get_is_stopping (session) &&
            !session_get_is_run (session))
            return session;
    }

    return NULL;
}

static void
display_server_ready_cb (DisplayServer *display_server, Seat *seat)
{
    /* Run setup script */
    const gchar *script = seat_get_string_property (seat, "display-setup-script");
    if (script && !run_script (seat, display_server, script, NULL))
    {
        l_debug (seat, "Stopping display server due to failed setup script");
        display_server_stop (display_server);
        return;
    }

    emit_upstart_signal ("login-session-start");

    /* Start the session waiting for this display server */
    Session *session = find_session_for_display_server (seat, display_server);
    if (session)
    {
        if (session_get_is_authenticated (session))
        {
            l_debug (seat, "Display server ready, running session");
            run_session (seat, session);
        }
        else
        {
            l_debug (seat, "Display server ready, starting session authentication");
            start_session (seat, session);
        }
    }
    else
    {
        l_debug (seat, "Stopping not required display server");
        display_server_stop (display_server);
    }
}

static DisplayServer *
create_display_server (Seat *seat, Session *session)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    l_debug (seat, "Creating display server of type %s", session_get_session_type (session));

    DisplayServer *display_server = SEAT_GET_CLASS (seat)->create_display_server (seat, session);
    if (!display_server)
        return NULL;

    /* Remember this display server */
    if (!g_list_find (priv->display_servers, display_server))
    {
        priv->display_servers = g_list_append (priv->display_servers, display_server);
        g_signal_connect (display_server, DISPLAY_SERVER_SIGNAL_READY, G_CALLBACK (display_server_ready_cb), seat);
        g_signal_connect (display_server, DISPLAY_SERVER_SIGNAL_STOPPED, G_CALLBACK (display_server_stopped_cb), seat);
    }

    return display_server;
}

static gboolean
start_display_server (Seat *seat, DisplayServer *display_server)
{
    if (display_server_get_is_ready (display_server))
    {
        display_server_ready_cb (display_server, seat);
        return TRUE;
    }
    else
        return display_server_start (display_server);
}

gboolean
seat_switch_to_greeter (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat_get_can_switch (seat) && priv->sessions != NULL)
        return FALSE;

    /* Switch to greeter if one open */
    GreeterSession *greeter_session = find_greeter_session (seat);
    if (greeter_session)
    {
        l_debug (seat, "Switching to existing greeter");
        seat_set_active_session (seat, SESSION (greeter_session));
        return TRUE;
    }

    greeter_session = create_greeter_session (seat);
    if (!greeter_session)
        return FALSE;

    g_clear_object (&priv->session_to_activate);
    priv->session_to_activate = g_object_ref (SESSION (greeter_session));

    DisplayServer *display_server = create_display_server (seat, SESSION (greeter_session));
    if (!display_server) {
        g_clear_object (&priv->session_to_activate);
        return FALSE;
    }
    session_set_display_server (SESSION (greeter_session), display_server);

    return start_display_server (seat, display_server);
}

static void
switch_authentication_complete_cb (Session *session, Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    /* If authenticated, then unlock existing session or start new one */
    if (session_get_is_authenticated (session))
    {
        Session *s = find_user_session (seat, session_get_username (session), session);
        if (s)
        {
            l_debug (seat, "Session authenticated, switching to existing user session");
            session_unlock (s);
            seat_set_active_session (seat, s);
            session_stop (session);
        }
        else
        {
            l_debug (seat, "Session authenticated, starting display server");
            g_clear_object (&priv->session_to_activate);
            priv->session_to_activate = g_object_ref (session);
            DisplayServer *display_server = create_display_server (seat, session);
            session_set_display_server (session, display_server);
            start_display_server (seat, display_server);
        }

        return;
    }

    session_stop (session);

    /* See if we already have a greeter up and reuse it if so */
    GreeterSession *greeter_session = find_resettable_greeter (seat);
    gboolean existing = FALSE;
    if (greeter_session)
    {
        l_debug (seat, "Switching to existing greeter to authenticate session");
        set_greeter_hints (seat, greeter_session_get_greeter (greeter_session));
        existing = TRUE;
    }
    else
    {
        l_debug (seat, "Starting greeter to authenticate session");
        greeter_session = create_greeter_session (seat);
    }
    Greeter *greeter = greeter_session_get_greeter (greeter_session);

    if (session_get_is_guest (session))
        greeter_set_hint (greeter, "select-guest", "true");
    else
        greeter_set_hint (greeter, "select-user", session_get_username (session));

    if (existing)
    {
        greeter_reset (greeter);
        seat_set_active_session (seat, SESSION (greeter_session));
    }
    else
    {
        g_clear_object (&priv->session_to_activate);
        priv->session_to_activate = g_object_ref (SESSION (greeter_session));

        DisplayServer *display_server = create_display_server (seat, SESSION (greeter_session));
        session_set_display_server (SESSION (greeter_session), display_server);
        start_display_server (seat, display_server);
    }
}

gboolean
seat_switch_to_user (Seat *seat, const gchar *username, const gchar *session_name)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    g_return_val_if_fail (seat != NULL, FALSE);
    g_return_val_if_fail (username != NULL, FALSE);

    if (!seat_get_can_switch (seat))
        return FALSE;

    /* If we're already on this session, then ignore */
    Session *session = find_user_session (seat, username, NULL);
    if (session && session == priv->active_session)
        return TRUE;

    l_debug (seat, "Switching to user %s", username);

    /* Attempt to authenticate them */
    session = create_user_session (seat, username, FALSE);
    g_signal_connect (session, SESSION_SIGNAL_AUTHENTICATION_COMPLETE, G_CALLBACK (switch_authentication_complete_cb), seat);
    session_set_pam_service (session, seat_get_string_property (seat, "pam-service"));

    return session_start (session);
}

static Session *
find_guest_session (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    for (GList *link = priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        if (!session_get_is_stopping (session) && session_get_is_guest (session))
            return session;
    }

    return NULL;
}

gboolean
seat_switch_to_guest (Seat *seat, const gchar *session_name)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat_get_can_switch (seat) || !seat_get_allow_guest (seat))
        return FALSE;

    /* Switch to session if one open */
    Session *session = find_guest_session (seat);
    if (session)
    {
        l_debug (seat, "Switching to existing guest account %s", session_get_username (session));
        seat_set_active_session (seat, session);
        return TRUE;
    }

    session = create_guest_session (seat, session_name);
    if (!session)
        return FALSE;

    DisplayServer *display_server = create_display_server (seat, session);

    g_clear_object (&priv->session_to_activate);
    priv->session_to_activate = g_object_ref (session);
    session_set_pam_service (session, seat_get_string_property (seat, "pam-autologin-service"));
    session_set_display_server (session, display_server);

    return start_display_server (seat, display_server);
}

gboolean
seat_lock (Seat *seat, const gchar *username)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat_get_can_switch (seat))
        return FALSE;

    // FIXME: If already locked then don't bother...

    l_debug (seat, "Locking");

    /* Switch to greeter we can reuse */
    gboolean reset_existing = FALSE;
    gboolean reuse_xserver = FALSE;
    DisplayServer *display_server = NULL;
    GreeterSession *greeter_session = find_resettable_greeter (seat);
    if (greeter_session)
    {
        l_debug (seat, "Switching to existing greeter");
        set_greeter_hints (seat, greeter_session_get_greeter (greeter_session));
        reset_existing = TRUE;
    }
    else
    {
        /* If the existing greeter can't be reused, stop it and reuse its display server */
        greeter_session = find_greeter_session (seat);
        if (greeter_session)
        {
            display_server = session_get_display_server (SESSION (greeter_session));
            if (!session_get_is_stopping (SESSION (greeter_session)))
            {
                l_debug (seat, "Stopping session");
                session_stop (SESSION (greeter_session));
            }
            reuse_xserver = TRUE;
        }

        greeter_session = create_greeter_session (seat);
        if (!greeter_session)
            return FALSE;
    }
    Greeter *greeter = greeter_session_get_greeter (greeter_session);

    greeter_set_hint (greeter, "lock-screen", "true");
    if (username)
        greeter_set_hint (greeter, "select-user", username);

    if (reset_existing)
    {
        greeter_reset (greeter);
        seat_set_active_session (seat, SESSION (greeter_session));
        return TRUE;
    }
    else
    {
        if (!reuse_xserver)
            display_server = create_display_server (seat, SESSION (greeter_session));
        session_set_display_server (SESSION (greeter_session), display_server);

        g_clear_object (&priv->session_to_activate);
        priv->session_to_activate = g_object_ref (SESSION (greeter_session));

        if (reuse_xserver)
        {
            g_clear_object (&priv->replacement_greeter);
            priv->replacement_greeter = g_object_ref (greeter_session);
            return TRUE;
        }
        else
            return start_display_server (seat, display_server);
    }
}

void
seat_stop (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    g_return_if_fail (seat != NULL);

    if (priv->stopping)
        return;

    l_debug (seat, "Stopping");
    priv->stopping = TRUE;
    SEAT_GET_CLASS (seat)->stop (seat);
}

gboolean
seat_get_is_stopping (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, FALSE);
    return priv->stopping;
}

static void
seat_real_setup (Seat *seat)
{
}

/**
 * @brief seat_real_start
 * 函数得到greeter_session后调用create_display_server方法。
 * create_display_server找到Seat对象上的create_display_server成员变量。
 * 这个成员变量是个函数指针，调用create_display_server指向的函数得到display_server
 */
static gboolean
seat_real_start (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    /* Get autologin settings */
    const gchar *autologin_username = seat_get_string_property (seat, "autologin-user");
    if (g_strcmp0 (autologin_username, "") == 0)
        autologin_username = NULL;
    int autologin_timeout = seat_get_integer_property (seat, "autologin-user-timeout");
    gboolean autologin_guest = seat_get_boolean_property (seat, "autologin-guest");
    gboolean autologin_in_background = seat_get_boolean_property (seat, "autologin-in-background");

    /* Autologin if configured */
    Session *session = NULL, *background_session = NULL;
    if (autologin_timeout == 0 || autologin_in_background)
    {
        if (autologin_guest)
            session = create_guest_session (seat, NULL);
        else if (autologin_username != NULL)
            session = create_user_session (seat, autologin_username, TRUE);

        if (session)
            session_set_pam_service (session, seat_get_string_property (seat, "pam-autologin-service"));

        /* Load in background if required */
        if (autologin_in_background && session)
        {
            background_session = session;
            session = NULL;
        }

        if (session)
        {
            g_clear_object (&priv->session_to_activate);
            priv->session_to_activate = g_object_ref (session);

            DisplayServer *display_server = create_display_server (seat, session);
            session_set_display_server (session, display_server);
            if (!display_server || !start_display_server (seat, display_server))
            {
                l_debug (seat, "Can't create display server for automatic login");
                session_stop (session);
                if (display_server)
                    display_server_stop (display_server);
                session = NULL;
            }
        }
    }

    /* Fallback to a greeter */
    if (!session)
    {
        GreeterSession *greeter_session = create_greeter_session (seat);
        if (!greeter_session)
        {
            l_debug (seat, "Failed to create greeter session");
            return FALSE;
        }

        g_clear_object (&priv->session_to_activate);
        session = SESSION (greeter_session);
        priv->session_to_activate = g_object_ref (session);

        DisplayServer *display_server = create_display_server (seat, session);
        session_set_display_server (session, display_server);
        if (!display_server || !start_display_server (seat, display_server))
        {
            l_debug (seat, "Can't create display server for greeter");
            session_stop (session);
            if (display_server)
                display_server_stop (display_server);
            session = NULL;
        }
    }

    /* Fail if can't start a session */
    if (!session)
    {
        seat_stop (seat);
        return FALSE;
    }

    /* Start background session */
    if (background_session)
    {
        DisplayServer *background_display_server = create_display_server (seat, background_session);
        session_set_display_server (background_session, background_display_server);
        if (!start_display_server (seat, background_display_server))
            l_warning (seat, "Failed to start display server for background session");
    }

    return TRUE;
}

static DisplayServer *
seat_real_create_display_server (Seat *seat, Session *session)
{
    return NULL;
}

static gboolean
seat_real_display_server_is_used (Seat *seat, DisplayServer *display_server)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    for (GList *link = priv->sessions; link; link = link->next)
    {
        Session *session = link->data;

        DisplayServer *d = session_get_display_server (session);
        if (!d)
            continue;

        if (d == display_server || display_server_get_parent (d) == display_server)
            return TRUE;
    }

    return FALSE;
}

static GreeterSession *
seat_real_create_greeter_session (Seat *seat)
{
    return greeter_session_new ();
}

static Session *
create_session_cb (Greeter *greeter, Seat *seat)
{
    return g_object_ref (create_session (seat, FALSE));
}

static Greeter *
create_greeter_cb (Session *session, Seat *seat)
{
    g_autoptr(Greeter) greeter = greeter_new ();

    greeter_set_pam_services (greeter,
                              seat_get_string_property (seat, "pam-service"),
                              seat_get_string_property (seat, "pam-autologin-service"));
    g_signal_connect (greeter, GREETER_SIGNAL_CREATE_SESSION, G_CALLBACK (create_session_cb), seat);
    g_signal_connect (greeter, GREETER_SIGNAL_START_SESSION, G_CALLBACK (greeter_start_session_cb), seat);

    /* Set hints to greeter */
    greeter_set_allow_guest (greeter, seat_get_allow_guest (seat));
    set_greeter_hints (seat, greeter);

    return g_steal_pointer (&greeter);
}

static Session *
seat_real_create_session (Seat *seat)
{
    g_autoptr(Session) session = session_new ();
    g_signal_connect (session, SESSION_SIGNAL_CREATE_GREETER, G_CALLBACK (create_greeter_cb), seat);

    return g_steal_pointer (&session);
}

static void
seat_real_set_active_session (Seat *seat, Session *session)
{
}

static void
seat_real_set_next_session (Seat *seat, Session *session)
{
}

static Session *
seat_real_get_active_session (Seat *seat)
{
    return NULL;
}

static void
seat_real_stop (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    check_stopped (seat);
    if (priv->stopped)
        return;

    /* Stop all the display servers and sessions on the seat. Copy the list as
     * it might be modified if a display server / session stops during this loop */
    GList *list = g_list_copy (priv->display_servers);
    for (GList *link = list; link; link = link->next)
        g_object_ref (link->data);
    for (GList *link = list; link; link = link->next)
    {
        DisplayServer *display_server = link->data;
        if (!display_server_get_is_stopping (display_server))
        {
            l_debug (seat, "Stopping display server");
            display_server_stop (display_server);
        }
    }
    g_list_free_full (list, g_object_unref);
    list = g_list_copy (priv->sessions);
    for (GList *link = list; link; link = link->next)
        g_object_ref (link->data);
    for (GList *link = list; link; link = link->next)
    {
        Session *session = link->data;
        if (!session_get_is_stopping (session))
        {
            l_debug (seat, "Stopping session");
            session_stop (session);
        }
    }
    g_list_free_full (list, g_object_unref);
}

static void
seat_init (Seat *seat)
{
    SeatPrivate *priv = seat_get_instance_private (seat);

    priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    priv->share_display_server = TRUE;
}

static void
seat_finalize (GObject *object)
{
    Seat *self = SEAT (object);
    SeatPrivate *priv = seat_get_instance_private (SEAT (self));

    g_free (priv->name);
    g_hash_table_unref (priv->properties);
    for (GList *link = priv->display_servers; link; link = link->next)
    {
        DisplayServer *display_server = link->data;
        g_signal_handlers_disconnect_matched (display_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    }
    g_list_free_full (priv->display_servers, g_object_unref);
    for (GList *link = priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        g_signal_handlers_disconnect_matched (session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    }
    g_list_free_full (priv->sessions, g_object_unref);
    g_clear_object (&priv->active_session);
    g_clear_object (&priv->next_session);
    g_clear_object (&priv->session_to_activate);
    g_clear_object (&priv->replacement_greeter);

    G_OBJECT_CLASS (seat_parent_class)->finalize (object);
}

static void
seat_class_init (SeatClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->setup = seat_real_setup;
    klass->start = seat_real_start;
    klass->create_display_server = seat_real_create_display_server;
    klass->display_server_is_used = seat_real_display_server_is_used;
    klass->create_greeter_session = seat_real_create_greeter_session;
    klass->create_session = seat_real_create_session;
    klass->set_active_session = seat_real_set_active_session;
    klass->get_active_session = seat_real_get_active_session;
    klass->set_next_session = seat_real_set_next_session;
    klass->run_script = seat_real_run_script;
    klass->stop = seat_real_stop;

    object_class->finalize = seat_finalize;

    signals[SESSION_ADDED] =
        g_signal_new (SEAT_SIGNAL_SESSION_ADDED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, session_added),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SESSION_TYPE);
    signals[RUNNING_USER_SESSION] =
        g_signal_new (SEAT_SIGNAL_RUNNING_USER_SESSION,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, running_user_session),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SESSION_TYPE);
    signals[SESSION_REMOVED] =
        g_signal_new (SEAT_SIGNAL_SESSION_REMOVED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, session_removed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SESSION_TYPE);
    signals[STOPPED] =
        g_signal_new (SEAT_SIGNAL_STOPPED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, stopped),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static gint
seat_real_logprefix (Logger *self, gchar *buf, gulong buflen)
{
    return g_snprintf (buf, buflen, "Seat %s: ", seat_get_name (SEAT (self)));
}

static void
seat_logger_iface_init (LoggerInterface *iface)
{
    iface->logprefix = &seat_real_logprefix;
}
