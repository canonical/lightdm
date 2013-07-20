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
#include "greeter.h"

enum {
    SESSION_ADDED,
    RUNNING_USER_SESSION,
    SESSION_REMOVED,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct SeatPrivate
{
    /* Configuration for this seat */
    GHashTable *properties;

    /* TRUE if able to switch users */
    gboolean can_switch;

    /* TRUE if display server can be shared for sessions */
    gboolean share_display_server;

    /* The display servers on this seat */
    GList *display_servers;

    /* The sessions on this seat */
    GList *sessions;

    /* The session to set active when it starts */
    Session *session_to_activate;

    /* TRUE if stopping this seat (waiting for displays to stop) */
    gboolean stopping;

    /* TRUE if stopped */
    gboolean stopped;
};

/* PAM services to use */
#define GREETER_SERVICE   "lightdm-greeter"
#define USER_SERVICE      "lightdm"
#define AUTOLOGIN_SERVICE "lightdm-autologin"

G_DEFINE_TYPE (Seat, seat, G_TYPE_OBJECT);

typedef struct
{
    const gchar *name;
    GType type;
} SeatModule;
static GHashTable *seat_modules = NULL;

// FIXME: Make a get_display_server() that re-uses display servers if supported
static DisplayServer *create_display_server (Seat *seat);
static Greeter *create_greeter_session (Seat *seat);
static void start_session (Seat *seat, Session *session);

void
seat_register_module (const gchar *name, GType type)
{
    SeatModule *module;

    if (!seat_modules)
        seat_modules = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    g_debug ("Registered seat module %s", name);

    module = g_malloc0 (sizeof (SeatModule));
    module->name = g_strdup (name);
    module->type = type;
    g_hash_table_insert (seat_modules, g_strdup (name), module);
}

Seat *
seat_new (const gchar *module_name)
{
    Seat *seat;
    SeatModule *m = NULL;
  
    g_return_val_if_fail (module_name != NULL, NULL);

    if (seat_modules)
        m = g_hash_table_lookup (seat_modules, module_name);
    if (!m)
        return NULL;

    seat = g_object_new (m->type, NULL);

    return seat;
}

void
seat_set_property (Seat *seat, const gchar *name, const gchar *value)
{
    g_return_if_fail (seat != NULL);
    g_hash_table_insert (seat->priv->properties, g_strdup (name), g_strdup (value));
}

const gchar *
seat_get_string_property (Seat *seat, const gchar *name)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return g_hash_table_lookup (seat->priv->properties, name);
}

gboolean
seat_get_boolean_property (Seat *seat, const gchar *name)
{
    return g_strcmp0 (seat_get_string_property (seat, name), "true") == 0;
}

gint
seat_get_integer_property (Seat *seat, const gchar *name)
{
    const gchar *value;

    value = seat_get_string_property (seat, name);
    return value ? atoi (value) : 0;
}

void
seat_set_can_switch (Seat *seat, gboolean can_switch)
{
    g_return_if_fail (seat != NULL);

    seat->priv->can_switch = can_switch;
}

void
seat_set_share_display_server (Seat *seat, gboolean share_display_server)
{
    g_return_if_fail (seat != NULL);

    seat->priv->share_display_server = share_display_server;
}

gboolean
seat_start (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
  
    SEAT_GET_CLASS (seat)->setup (seat);
    return SEAT_GET_CLASS (seat)->start (seat);
}

GList *
seat_get_sessions (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return seat->priv->sessions;
}

void
seat_set_active_session (Seat *seat, Session *session)
{
    GList *link;

    g_return_if_fail (seat != NULL);

    SEAT_GET_CLASS (seat)->set_active_session (seat, session);

    /* Stop any greeters */
    for (link = seat->priv->sessions; link; link = link->next)
    {
        Session *s = link->data;

        if (s == session || session_get_is_stopping (s))
            continue;

        if (IS_GREETER (s))
        {
            g_debug ("Stopping greeter");
            session_stop (s);
        }
    }
}

Session *
seat_get_active_session (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return SEAT_GET_CLASS (seat)->get_active_session (seat);
}

gboolean
seat_get_can_switch (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return seat->priv->can_switch;
}

gboolean
seat_get_allow_guest (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return seat_get_boolean_property (seat, "allow-guest") && guest_account_is_installed ();
}

gboolean
seat_get_greeter_allow_guest (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);  
    return seat_get_allow_guest (seat) && seat_get_boolean_property (seat, "greeter-allow-guest");
}

static gboolean
run_script (Seat *seat, DisplayServer *display_server, const gchar *script_name, User *user)
{
    Process *script;
    gboolean result = FALSE;
  
    script = process_new ();

    process_set_command (script, script_name);

    /* Set POSIX variables */
    process_set_clear_environment (script, TRUE);
    process_set_env (script, "SHELL", "/bin/sh");

    /* Variables required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
    {
        process_set_env (script, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));
        process_set_env (script, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
        process_set_env (script, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
        process_set_env (script, "PATH", g_getenv ("PATH"));
    }
    else
        process_set_env (script, "PATH", "/usr/local/bin:/usr/bin:/bin");

    if (user)
    {
        process_set_env (script, "USER", user_get_name (user));
        process_set_env (script, "LOGNAME", user_get_name (user));
        process_set_env (script, "HOME", user_get_home_directory (user));
    }
    else
        process_set_env (script, "HOME", "/");

    SEAT_GET_CLASS (seat)->run_script (seat, display_server, script);

    if (process_start (script, TRUE))
    {
        int exit_status;

        exit_status = process_get_exit_status (script);
        if (WIFEXITED (exit_status))
        {
            g_debug ("Exit status of %s: %d", script_name, WEXITSTATUS (exit_status));
            result = WEXITSTATUS (exit_status) == EXIT_SUCCESS;
        }
    }

    g_object_unref (script);

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

    gchar *cmd = g_strdup_printf ("initctl -q emit %s DISPLAY_MANAGER=lightdm", signal);
    g_spawn_command_line_async (cmd, NULL); /* OK if it fails, probably not installed */
    g_free (cmd);
}

static void
check_stopped (Seat *seat)
{
    if (seat->priv->stopping &&
        !seat->priv->stopped &&
        g_list_length (seat->priv->display_servers) == 0 &&
        g_list_length (seat->priv->sessions) == 0)
    {
        seat->priv->stopped = TRUE;
        g_debug ("Seat stopped");
        g_signal_emit (seat, signals[STOPPED], 0);
    }
}

static void
display_server_stopped_cb (DisplayServer *display_server, Seat *seat)
{
    GList *list, *link;
    Session *active_session;

    g_debug ("Display server stopped");

    g_signal_handlers_disconnect_matched (display_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    seat->priv->display_servers = g_list_remove (seat->priv->display_servers, display_server);

    if (seat->priv->stopping)
    {
        check_stopped (seat);
        g_object_unref (display_server);
        return;
    }

    /* Stop all sessions on this display server */
    list = g_list_copy (seat->priv->sessions);
    for (link = list; link; link = link->next)
        g_object_ref (link->data);
    for (link = list; link; link = link->next)
    {
        Session *session = link->data;

        if (session_get_display_server (session) != display_server || session_get_is_stopping (session))
            continue;

        /* Stop seat if this is the only display server and it failed to start a greeter */
        if (IS_GREETER (session) &&
            !session_get_is_started (session) &&
            g_list_length (seat->priv->display_servers) == 0)
        {
            g_debug ("Stopping seat, greeter display server failed to start");
            seat_stop (seat);
        }

        g_debug ("Stopping session");
        session_stop (session);
    }
    g_list_free_full (list, g_object_unref);

    if (!seat->priv->stopping)
    {
        /* If we were the active session, switch to a greeter */
        active_session = seat_get_active_session (seat);
        if (!active_session || session_get_display_server (active_session) == display_server)
        {
            g_debug ("Active display server stopped, starting greeter");
            seat_switch_to_greeter (seat);
        }
    }

    g_object_unref (display_server);
}

static void
switch_to_greeter_from_failed_session (Seat *seat, Session *session)
{
    Greeter *greeter_session;
    DisplayServer *display_server;

    greeter_session = create_greeter_session (seat);
    if (session_get_is_guest (session))
        greeter_set_hint (greeter_session, "select-guest", "true");
    else
        greeter_set_hint (greeter_session, "select-user", session_get_username (session));
    if (seat->priv->session_to_activate)
        g_object_unref (seat->priv->session_to_activate);
    seat->priv->session_to_activate = g_object_ref (greeter_session);

    if (seat->priv->share_display_server)
        session_set_display_server (SESSION (greeter_session), session_get_display_server (session));
    else
    {
        DisplayServer *display_server;

        display_server = create_display_server (seat);
        if (!display_server_start (display_server))
        {
            g_debug ("Failed to start display server for greeter");
            seat_stop (seat);
        }

        session_set_display_server (session, display_server);
    }

    start_session (seat, SESSION (greeter_session));

    /* Stop failed session */
    session_stop (session);
}

static void
start_session (Seat *seat, Session *session)
{
    Greeter *greeter_session;

    if (session_start (session))
        return;

    if (IS_GREETER (session))
    {
        g_debug ("Failed to start greeter");
        display_server_stop (session_get_display_server (session));
        return;
    }

    g_debug ("Failed to start session, starting greeter");
    switch_to_greeter_from_failed_session (seat, session);
}

static void
run_session (Seat *seat, Session *session)
{
    const gchar *script;

    if (IS_GREETER (session))
        script = seat_get_string_property (seat, "greeter-setup-script");
    else
        script = seat_get_string_property (seat, "session-setup-script");
    if (script && !run_script (seat, session_get_display_server (session), script, NULL))
    {
        g_debug ("Switching to greeter due to failed setup script");
        switch_to_greeter_from_failed_session (seat, session);
        return;
    }

    if (!IS_GREETER (session))
        g_signal_emit (seat, signals[RUNNING_USER_SESSION], 0, session);

    session_run (session);

    // FIXME: Wait until the session is ready

    if (session == seat->priv->session_to_activate)
    {
        seat_set_active_session (seat, session);
        g_object_unref (seat->priv->session_to_activate);
        seat->priv->session_to_activate = NULL;
    }
}

static void
session_authentication_complete_cb (Session *session, Seat *seat)
{
    if (session_get_is_authenticated (session))
    {
        g_debug ("Session authenticated, running command");
        run_session (seat, session);
    }
    else if (!IS_GREETER (session))
    {
        g_debug ("Switching to greeter due to failed authentication");
        switch_to_greeter_from_failed_session (seat, session);
    }
    else
    {
        g_debug ("Stopping session that failed authentication");
        session_stop (session);
    }
}

static void
session_stopped_cb (Session *session, Seat *seat)
{
    DisplayServer *display_server;

    g_debug ("Session stopped");

    display_server = session_get_display_server (session);

    /* Cleanup */
    if (!IS_GREETER (session))
    {
        const gchar *script;
        script = seat_get_string_property (seat, "session-cleanup-script");
        if (script)
            run_script (seat, display_server, script, session_get_user (session));
    }

    g_signal_handlers_disconnect_matched (session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    seat->priv->sessions = g_list_remove (seat->priv->sessions, session);

    /* We were waiting for this session, but it didn't start :( */
    // FIXME: Start a greeter on this?
    if (session == seat->priv->session_to_activate)
    {
        g_object_unref (seat->priv->session_to_activate);
        seat->priv->session_to_activate = NULL;
    }

    if (seat->priv->stopping)
    {
        check_stopped (seat);
        g_object_unref (session);
        return;
    }

    /* If this is the greeter session then re-use this display server */
    if (IS_GREETER (session) && seat->priv->share_display_server &&
        greeter_get_start_session (GREETER (session)))
    {
        GList *link;

        for (link = seat->priv->sessions; link; link = link->next)
        {
            Session *s = link->data;

            /* Skip this session and sessions on other display servers */
            if (s == session || session_get_display_server (s) != display_server || session_get_is_stopping (s))
                continue;

            if (session_get_is_authenticated (s))
            {
                g_debug ("Greeter stopped, running session");
                run_session (seat, s);
            }
            else
            {
                g_debug ("Greeter stopped, starting session authentication");
                start_session (seat, s);
            }
            break;
        }
    }
    /* If this is the greeter and nothing else is running then stop the seat */
    else if (IS_GREETER (session) &&
        !greeter_get_start_session (GREETER (session)) &&
        g_list_length (seat->priv->display_servers) == 1 &&
        g_list_nth_data (seat->priv->display_servers, 0) == display_server)
    {
        g_debug ("Stopping seat, failed to start a greeter");
        seat_stop (seat);
    }
    /* If we were the active session, switch to a greeter */
    else if (!IS_GREETER (session) && session == seat_get_active_session (seat))
    {
        g_debug ("Active session stopped, starting greeter");
        seat_switch_to_greeter (seat);
    }

    /* Stop the display server if no-longer required */
    if (display_server && !display_server_get_is_stopping (display_server))
    {
        GList *link;
        int n_sessions = 0;

        for (link = seat->priv->sessions; link; link = link->next)
        {
            Session *s = link->data;
            if (s == session)
                continue;
            if (session_get_display_server (s) == display_server)
                n_sessions++;
        }
        if (n_sessions == 0)
        {
            g_debug ("Stopping display server, no sessions require it");
            display_server_stop (display_server);
        }
    }

    g_signal_emit (seat, signals[SESSION_REMOVED], 0, session);
    g_object_unref (session);
}

static void
set_session_env (Session *session)
{
    /* Variables required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
    {
        session_set_env (session, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));
        session_set_env (session, "DBUS_SYSTEM_BUS_ADDRESS", g_getenv ("DBUS_SYSTEM_BUS_ADDRESS"));
        session_set_env (session, "DBUS_SESSION_BUS_ADDRESS", g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
        session_set_env (session, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
        session_set_env (session, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
        session_set_env (session, "GI_TYPELIB_PATH", g_getenv ("GI_TYPELIB_PATH"));
    }
}

static Session *
create_session (Seat *seat, gboolean autostart)
{
    Session *session;

    session = SEAT_GET_CLASS (seat)->create_session (seat);
    seat->priv->sessions = g_list_append (seat->priv->sessions, session);
    if (autostart)
        g_signal_connect (session, "authentication-complete", G_CALLBACK (session_authentication_complete_cb), seat);
    g_signal_connect (session, "stopped", G_CALLBACK (session_stopped_cb), seat);

    set_session_env (session);

    g_signal_emit (seat, signals[SESSION_ADDED], 0, session);

    return session;
}

static gchar **
get_session_argv_from_filename (const gchar *filename, const gchar *session_wrapper)
{
    GKeyFile *session_desktop_file;
    gboolean result;
    int argc;
    gchar *command = NULL, **argv, *path;
    GError *error = NULL;

    /* Read the command from the .desktop file */
    session_desktop_file = g_key_file_new ();
    result = g_key_file_load_from_file (session_desktop_file, filename, G_KEY_FILE_NONE, &error);
    if (error)
        g_debug ("Failed to load session file %s: %s", filename, error->message);
    g_clear_error (&error);
    if (result)
    {
        command = g_key_file_get_string (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
        if (!command)
            g_debug ("No command in session file %s", filename);
    }
    g_key_file_free (session_desktop_file);

    if (!command)
        return NULL;

    /* If configured, run sessions through a wrapper */
    if (session_wrapper)
    {
        argv = g_malloc (sizeof (gchar *) * 3);
        path = g_find_program_in_path (session_wrapper);
        argv[0] = path ? path : g_strdup (session_wrapper);
        argv[1] = command;
        argv[2] = NULL;
        return argv;
    }

    /* Split command into an array listing and make command absolute */
    result = g_shell_parse_argv (command, &argc, &argv, &error);
    if (error)
        g_debug ("Invalid session command '%s': %s", command, error->message);
    g_clear_error (&error);
    g_free (command);
    if (!result)
        return NULL;
    path = g_find_program_in_path (argv[0]);
    if (path)
    {
        g_free (argv[0]);
        argv[0] = path;
    }
  
    return argv;
}

static gchar **
get_session_argv (const gchar *sessions_dir, const gchar *session_name, const gchar *session_wrapper)
{
    gchar **dirs, **argv;
    int i;

    g_return_val_if_fail (sessions_dir != NULL, NULL);
    g_return_val_if_fail (session_name != NULL, NULL);

    dirs = g_strsplit (sessions_dir, ":", -1);
    for (i = 0; dirs[i]; i++)
    {
        gchar *filename, *path;

        filename = g_strdup_printf ("%s.desktop", session_name);
        path = g_build_filename (dirs[i], filename, NULL);
        g_free (filename);
        argv = get_session_argv_from_filename (path, session_wrapper);
        g_free (path);
        if (argv)
            break;
    }
    g_strfreev (dirs);

    return argv;
}

static Session *
create_user_session (Seat *seat, const gchar *username)
{
    User *user;
    gchar *sessions_dir, **argv;
    const gchar *session_name;
    Session *session;
  
    user = accounts_get_user_by_name (username);
    if (!user)
    {
        g_debug ("Can't login unknown user '%s'", username);
        return NULL;
    }

    session_name = user_get_xsession (user);
    g_object_unref (user);
    if (!session_name)
        session_name = seat_get_string_property (seat, "user-session");
    sessions_dir = config_get_string (config_get_instance (), "LightDM", "sessions-directory");
    argv = get_session_argv (sessions_dir, session_name, seat_get_string_property (seat, "session-wrapper"));
    g_free (sessions_dir);
    if (!argv)
    {
        g_debug ("Can't find session '%s'", seat_get_string_property (seat, "user-session"));
        return NULL;
    }

    session = create_session (seat, TRUE);
    session_set_env (session, "DESKTOP_SESSION", session_name);
    session_set_env (session, "GDMSESSION", session_name);
    session_set_pam_service (session, AUTOLOGIN_SERVICE);
    session_set_username (session, username);
    session_set_do_authenticate (session, TRUE);
    session_set_argv (session, argv);

    return session;
}

static Session *
create_guest_session (Seat *seat)
{
    gchar *sessions_dir, **argv;
    Session *session;

    sessions_dir = config_get_string (config_get_instance (), "LightDM", "sessions-directory");
    argv = get_session_argv (sessions_dir,
                             seat_get_string_property (seat, "user-session"),
                             seat_get_string_property (seat, "session-wrapper"));
    g_free (sessions_dir);
    if (!argv)
    {
        g_debug ("Can't find session '%s'", seat_get_string_property (seat, "user-session"));
        return NULL;
    }

    session = create_session (seat, TRUE);
    session_set_do_authenticate (session, TRUE);
    session_set_is_guest (session, TRUE);
    session_set_argv (session, argv);
  
    return session;
}

static Session *
greeter_create_session_cb (Greeter *greeter, Seat *seat)
{
    Session *session;

    session = create_session (seat, FALSE);
    session_set_display_server (session, session_get_display_server (SESSION (greeter)));

    return g_object_ref (session);
}

static void
prepend_argv (gchar ***argv, const gchar *value)
{
    gchar **old_argv, **new_argv;
    gint i;

    old_argv = *argv;
    new_argv = g_malloc (sizeof (gchar *) * (g_strv_length (*argv) + 2));
    new_argv[0] = g_strdup (value);
    for (i = 0; old_argv[i]; i++)
        new_argv[i + 1] = old_argv[i];
    new_argv[i + 1] = NULL;

    g_free (*argv);
    *argv = new_argv;
}

static Session *
find_user_session (Seat *seat, const gchar *username)
{
    GList *link;

    if (!username)
        return NULL;

    for (link = seat->priv->sessions; link; link = link->next)
    {
        Session *session = link->data;

        if (!session_get_is_stopping (session) && strcmp (session_get_username (session), username) == 0)
            return session;
    }

    return NULL;
}

static gboolean
greeter_start_session_cb (Greeter *greeter, SessionType type, const gchar *session_name, Seat *seat)
{
    Session *session, *existing_session;
    const gchar *username;
    User *user;
    gchar *sessions_dir = NULL;
    gchar **argv;

    /* Get the session to use */
    if (greeter_get_guest_authenticated (greeter))
    {
        session = create_guest_session (seat);
        session_set_pam_service (session, AUTOLOGIN_SERVICE);
    }
    else
        session = greeter_get_authentication_session (greeter);

    /* Switch to this session when it is ready */
    if (seat->priv->session_to_activate)
        g_object_unref (seat->priv->session_to_activate);
    seat->priv->session_to_activate = g_object_ref (session);

    /* Return to existing session if it is open */
    username = session_get_username (session);
    existing_session = find_user_session (seat, username);
    if (existing_session && session != existing_session)
    {
        g_debug ("Returning to existing user session %s", username);
        session_stop (session);
        seat_set_active_session (seat, existing_session);
        return TRUE;
    }

    /* Get session command to run */
    switch (type)
    {
    case SESSION_TYPE_LOCAL:
        sessions_dir = config_get_string (config_get_instance (), "LightDM", "sessions-directory");
        break;
    case SESSION_TYPE_REMOTE:
        sessions_dir = config_get_string (config_get_instance (), "LightDM", "remote-sessions-directory");
        break;
    }
    user = session_get_user (session);
    if (!session_name && user)
        session_name = user_get_xsession (user);
    if (!session_name)
        session_name = seat_get_string_property (seat, "user-session");
    if (user)
        user_set_xsession (session_get_user (session), session_name);

    argv = get_session_argv (sessions_dir, session_name, seat_get_string_property (seat, "session-wrapper"));
    g_free (sessions_dir);
  
    if (!argv)
    {
        g_debug ("Can't find session '%s'", seat_get_string_property (seat, "user-session"));
        return FALSE;
    }

    session_set_argv (session, argv);
    g_strfreev (argv);
    session_set_env (session, "DESKTOP_SESSION", session_name);
    session_set_env (session, "GDMSESSION", session_name);

    /* If can re-use the display server, stop the greeter first */
    if (seat->priv->share_display_server)
    {
        /* Run on the same display server after the greeter has stopped */
        session_set_display_server (session, session_get_display_server (SESSION (greeter)));

        g_debug ("Stopping greeter");
        session_stop (SESSION (greeter));

        return TRUE;
    }
    /* Otherwise start a new display server for this session */
    else
    {
        DisplayServer *display_server;

        display_server = create_display_server (seat);
        if (!display_server_start (display_server))
            return FALSE;

        session_set_display_server (session, display_server);

        return TRUE;
    }
}

static Greeter *
create_greeter_session (Seat *seat)
{
    gchar *sessions_dir, **argv;
    Greeter *greeter_session;
    gchar *greeter_user;
    const gchar *greeter_wrapper;

    sessions_dir = config_get_string (config_get_instance (), "LightDM", "greeters-directory");
    argv = get_session_argv (sessions_dir,
                             seat_get_string_property (seat, "greeter-session"),
                             seat_get_string_property (seat, "session-wrapper"));
    g_free (sessions_dir);
    if (!argv)
        return NULL;

    greeter_wrapper = seat_get_string_property (seat, "greeter-wrapper");
    if (greeter_wrapper)
    {
        gchar *path;
        path = g_find_program_in_path (greeter_wrapper);
        prepend_argv (&argv, path ? path : greeter_wrapper);
        g_free (path);
    }

    greeter_session = SEAT_GET_CLASS (seat)->create_greeter_session (seat);
    seat->priv->sessions = g_list_append (seat->priv->sessions, SESSION (greeter_session));
    g_signal_connect (greeter_session, "authentication-complete", G_CALLBACK (session_authentication_complete_cb), seat);
    g_signal_connect (greeter_session, "stopped", G_CALLBACK (session_stopped_cb), seat);
  
    set_session_env (SESSION (greeter_session));

    session_set_pam_service (SESSION (greeter_session), GREETER_SERVICE);
    greeter_user = config_get_string (config_get_instance (), "LightDM", "greeter-user");
    session_set_username (SESSION (greeter_session), greeter_user);
    g_free (greeter_user);
    session_set_argv (SESSION (greeter_session), argv);

    greeter_set_pam_services (greeter_session, USER_SERVICE, AUTOLOGIN_SERVICE);
    greeter_set_allow_guest (greeter_session, seat_get_allow_guest (seat));
    g_signal_connect (greeter_session, "create-session", G_CALLBACK (greeter_create_session_cb), seat);
    g_signal_connect (greeter_session, "start-session", G_CALLBACK (greeter_start_session_cb), seat);

    return greeter_session;
}

static Session *
find_session_for_display_server (Seat *seat, DisplayServer *display_server)
{
    GList *link;

    for (link = seat->priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        if (session_get_display_server (session) == display_server && !session_get_is_stopping (session))
            return session;
    }

    return NULL;
}

static void
display_server_ready_cb (DisplayServer *display_server, Seat *seat)
{
    const gchar *script;
    Session *session;

    /* Run setup script */
    script = seat_get_string_property (seat, "display-setup-script");
    if (script && !run_script (seat, display_server, script, NULL))
    {
        g_debug ("Stopping display server due to failed setup script");
        display_server_stop (display_server);
        return;
    }

    emit_upstart_signal ("login-session-start");

    /* Start the session waiting for this display server */
    session = find_session_for_display_server (seat, display_server);
    if (session)
    {
        if (session_get_is_authenticated (session))
        {
            g_debug ("Display server ready, running session");
            run_session (seat, session);
        }
        else
        {
            g_debug ("Display server ready, starting session authentication");
            start_session (seat, session);
        }
    }
    else
    {
        g_debug ("Stopping not required display server");
        display_server_stop (display_server);
    }
}

static DisplayServer *
create_display_server (Seat *seat)
{
    DisplayServer *display_server;

    display_server = SEAT_GET_CLASS (seat)->create_display_server (seat);
    seat->priv->display_servers = g_list_append (seat->priv->display_servers, display_server);
    g_signal_connect (display_server, "ready", G_CALLBACK (display_server_ready_cb), seat);
    g_signal_connect (display_server, "stopped", G_CALLBACK (display_server_stopped_cb), seat);

    return display_server;
}

static Greeter *
find_greeter_session (Seat *seat)
{
    GList *link;

    for (link = seat->priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        if (!session_get_is_stopping (session) && IS_GREETER (session))
            return GREETER (session);
    }

    return NULL;
}

gboolean
seat_switch_to_greeter (Seat *seat)
{
    Greeter *greeter_session;
    DisplayServer *display_server;

    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    /* Switch to greeter if one open (shouldn't be though) */
    greeter_session = find_greeter_session (seat);
    if (greeter_session)
    {
        g_debug ("Switching to existing greeter");
        seat_set_active_session (seat, SESSION (greeter_session));
        return TRUE;
    }

    greeter_session = create_greeter_session (seat);
    if (seat->priv->session_to_activate)
        g_object_unref (seat->priv->session_to_activate);
    seat->priv->session_to_activate = g_object_ref (greeter_session);

    display_server = create_display_server (seat);
    session_set_display_server (SESSION (greeter_session), display_server);
    if (!display_server_start (display_server))
        return FALSE;

    return TRUE;
}

gboolean
seat_switch_to_user (Seat *seat, const gchar *username, const gchar *session_name)
{
    Session *session;
    DisplayServer *display_server;

    g_return_val_if_fail (seat != NULL, FALSE);
    g_return_val_if_fail (username != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Switching to user %s", username);

    session = find_user_session (seat, username);
    if (session)
    {
        g_debug ("Switching to existing user session %s", username);
        seat_set_active_session (seat, session);
        return TRUE;
    }

    session = create_user_session (seat, username);
    if (seat->priv->session_to_activate)
        g_object_unref (seat->priv->session_to_activate);
    seat->priv->session_to_activate = g_object_ref (session);
    session_set_pam_service (session, USER_SERVICE);

    display_server = create_display_server (seat);
    session_set_display_server (session, display_server);
    if (!display_server_start (display_server))
        return FALSE;

    return FALSE;
}

static Session *
find_guest_session (Seat *seat)
{
    GList *link;

    for (link = seat->priv->sessions; link; link = link->next)
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
    Session *session;
    DisplayServer *display_server;
    GList *link;

    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch || !seat_get_allow_guest (seat))
        return FALSE;

    /* Switch to session if one open */
    session = find_guest_session (seat);
    if (session)
    {
        g_debug ("Switching to existing guest account %s", session_get_username (session));
        seat_set_active_session (seat, session);
        return TRUE;
    }

    display_server = create_display_server (seat);
    if (!display_server_start (display_server))
        return FALSE;

    session = create_guest_session (seat);
    if (seat->priv->session_to_activate)
        g_object_unref (seat->priv->session_to_activate);
    seat->priv->session_to_activate = g_object_ref (session);
    session_set_pam_service (session, AUTOLOGIN_SERVICE);
    session_set_display_server (session, display_server);

    return TRUE;
}

gboolean
seat_lock (Seat *seat, const gchar *username)
{
    Greeter *greeter_session;
    DisplayServer *display_server;

    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Locking seat");

    /* Switch to greeter if one open (shouldn't be though) */
    greeter_session = find_greeter_session (seat);
    if (greeter_session)
    {
        g_debug ("Switching to existing greeter");
        seat_set_active_session (seat, SESSION (greeter_session));
        return TRUE;
    }

    display_server = create_display_server (seat);
    if (!display_server_start (display_server))
        return FALSE;

    greeter_session = create_greeter_session (seat);
    if (seat->priv->session_to_activate)
        g_object_unref (seat->priv->session_to_activate);
    seat->priv->session_to_activate = g_object_ref (greeter_session);
    greeter_set_hint (greeter_session, "lock-screen", "true");
    if (username)
        greeter_set_hint (greeter_session, "select-user", username);
    session_set_display_server (SESSION (greeter_session), display_server);

    return TRUE;
}

void
seat_stop (Seat *seat)
{
    g_return_if_fail (seat != NULL);

    if (seat->priv->stopping)
        return;

    g_debug ("Stopping seat");
    seat->priv->stopping = TRUE;
    SEAT_GET_CLASS (seat)->stop (seat);
}

gboolean
seat_get_is_stopping (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return seat->priv->stopping;
}

static void
seat_real_setup (Seat *seat)
{
}

static gboolean
seat_real_start (Seat *seat)
{
    const gchar *autologin_username;
    int autologin_timeout;
    gboolean autologin_guest;
    gboolean autologin_in_background;
    const gchar *user_session;
    Session *session = NULL;
    DisplayServer *display_server;
    User *user;
    gchar *sessions_dir;
    const gchar *wrapper = NULL;
    const gchar *session_name = NULL;

    g_debug ("Starting seat");

    display_server = create_display_server (seat);

    /* Get autologin settings */
    autologin_username = seat_get_string_property (seat, "autologin-user");
    if (g_strcmp0 (autologin_username, "") == 0)
        autologin_username = NULL;
    autologin_timeout = seat_get_integer_property (seat, "autologin-user-timeout");
    autologin_guest = seat_get_boolean_property (seat, "autologin-guest");
    autologin_in_background = seat_get_boolean_property (seat, "autologin-in-background");

    /* Autologin if configured */
    if (autologin_timeout == 0 || autologin_in_background)
    {
        if (autologin_guest)
        {
            session = create_guest_session (seat);
            if (seat->priv->session_to_activate)
                g_object_unref (seat->priv->session_to_activate);
            seat->priv->session_to_activate = g_object_ref (session);
            session_set_pam_service (session, AUTOLOGIN_SERVICE);
        }
        else if (autologin_username != NULL)
        {
            session = create_user_session (seat, autologin_username);
            if (seat->priv->session_to_activate)
                g_object_unref (seat->priv->session_to_activate);
            seat->priv->session_to_activate = g_object_ref (session);
            session_set_pam_service (session, AUTOLOGIN_SERVICE);
        }

        /* Load in background if required */
        if (autologin_in_background && session)
        {
            DisplayServer *background_display_server;

            background_display_server = create_display_server (seat);
            session_set_display_server (session, background_display_server);
            if (!display_server_start (background_display_server))
                return FALSE;

            /* Start a greeter as well */
            session = NULL;
        }
    }

    /* Fallback to a greeter */
    if (!session)
    {
        Greeter *greeter_session;

        greeter_session = create_greeter_session (seat);
        if (seat->priv->session_to_activate)
            g_object_unref (seat->priv->session_to_activate);
        seat->priv->session_to_activate = g_object_ref (greeter_session);
        session = SESSION (greeter_session);

        if (autologin_timeout)
        {
            gchar *value;

            value = g_strdup_printf ("%d", autologin_timeout);
            greeter_set_hint (greeter_session, "autologin-timeout", value);
            g_free (value);
            if (autologin_username)
                greeter_set_hint (greeter_session, "autologin-user", autologin_username);
            if (autologin_guest)
                greeter_set_hint (greeter_session, "autologin-guest", "true");
        }
    }

    /* Fail if can't start a session */
    if (!session)
    {
        seat_stop (seat);
        return FALSE;
    }

    /* Start display server to show session on */
    session_set_display_server (session, display_server);
    if (!display_server_start (display_server))
        return FALSE;

    return TRUE;
}

static void
seat_real_set_active_session (Seat *seat, Session *session)
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
    GList *list, *link;

    check_stopped (seat);
    if (seat->priv->stopped)
        return;

    /* Stop all the display servers and sessions on the seat. Copy the list as
     * it might be modified if a display server / session stops during this loop */
    list = g_list_copy (seat->priv->display_servers);
    for (link = list; link; link = link->next)
        g_object_ref (link->data);
    for (link = list; link; link = link->next)
    {
        DisplayServer *display_server = link->data;
        if (!display_server_get_is_stopping (display_server))
        {
            g_debug ("Stopping display server");
            display_server_stop (display_server);
        }
    }
    g_list_free_full (list, g_object_unref);
    list = g_list_copy (seat->priv->sessions);
    for (link = list; link; link = link->next)
        g_object_ref (link->data);
    for (link = list; link; link = link->next)
    {
        Session *session = link->data;
        if (!session_get_is_stopping (session))
        {
            g_debug ("Stopping session");
            session_stop (session);
        }
    }
    g_list_free_full (list, g_object_unref);
}

static void
seat_init (Seat *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_TYPE, SeatPrivate);
    seat->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    seat->priv->share_display_server = TRUE;
}

static void
seat_finalize (GObject *object)
{
    Seat *self;
    GList *link;

    self = SEAT (object);

    g_hash_table_unref (self->priv->properties);
    for (link = self->priv->display_servers; link; link = link->next)
    {
        DisplayServer *display_server = link->data;
        g_signal_handlers_disconnect_matched (display_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    }  
    g_list_free_full (self->priv->display_servers, g_object_unref);
    for (link = self->priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        g_signal_handlers_disconnect_matched (session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    }
    g_list_free_full (self->priv->sessions, g_object_unref);
    if (self->priv->session_to_activate)
        g_object_unref (self->priv->session_to_activate);

    G_OBJECT_CLASS (seat_parent_class)->finalize (object);
}

static void
seat_class_init (SeatClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->setup = seat_real_setup;
    klass->start = seat_real_start;
    klass->set_active_session = seat_real_set_active_session;
    klass->get_active_session = seat_real_get_active_session;
    klass->run_script = seat_real_run_script;
    klass->stop = seat_real_stop;

    object_class->finalize = seat_finalize;

    g_type_class_add_private (klass, sizeof (SeatPrivate));

    signals[SESSION_ADDED] =
        g_signal_new ("session-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, session_added),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SESSION_TYPE);
    signals[RUNNING_USER_SESSION] =
        g_signal_new ("running-user-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, running_user_session),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SESSION_TYPE);
    signals[SESSION_REMOVED] =
        g_signal_new ("session-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, session_removed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SESSION_TYPE);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, stopped),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}
