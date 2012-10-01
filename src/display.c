/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*-
 *
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gdesktopappinfo.h>

#include "display.h"
#include "configuration.h"
#include "ldm-marshal.h"
#include "greeter.h"

enum
{
    CREATE_SESSION,
    READY,
    SWITCH_TO_USER,
    SWITCH_TO_GUEST,
    GET_GUEST_USERNAME,
    DISPLAY_SERVER_READY,
    START_GREETER,
    START_SESSION,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct DisplayPrivate
{
    /* Display server */
    DisplayServer *display_server;

    /* Greeter session */
    gchar *greeter_session;

    /* TRUE if the user list should be shown */
    gboolean greeter_hide_users;

    /* TRUE if a manual login option should be shown */
    gboolean greeter_show_manual_login;

    /* TRUE if a remote login option should be shown */
    gboolean greeter_show_remote_login;

    /* TRUE if the greeter is a lock screen */
    gboolean greeter_is_lock;

    /* Session requested to log into */
    SessionType user_session_type;
    gchar *user_session;

    /* Program to run sessions through */
    gchar *session_wrapper;

    /* TRUE if in a user session */
    gboolean in_user_session;

    /* TRUE if have got an X server / started a greeter */
    gboolean is_ready;

    /* Session process */
    Session *session;

    /* Communication link to greeter */
    Greeter *greeter;

    /* User that should be automatically logged in */
    gchar *autologin_user;
    gboolean autologin_guest;
    gint autologin_timeout;

    /* TRUE if start greeter if fail to login */
    gboolean start_greeter_if_fail;

    /* Hint to select user in greeter */
    gchar *select_user_hint;
    gboolean select_guest_hint;

    /* TRUE if allowed to log into guest account */
    gboolean allow_guest;

    /* TRUE if greeter is to show the guest account */
    gboolean greeter_allow_guest;

    /* TRUE if stopping the display (waiting for dispaly server, greeter and session to stop) */
    gboolean stopping;

    /* TRUE if stopped */
    gboolean stopped;
};

/* PAM services to use */
#define GREETER_SERVICE   "lightdm-greeter"
#define USER_SERVICE      "lightdm"
#define AUTOLOGIN_SERVICE "lightdm-autologin"

G_DEFINE_TYPE (Display, display, G_TYPE_OBJECT);

static void greeter_session_stopped_cb (Session *session, Display *display);
static void user_session_stopped_cb (Session *session, Display *display);

Display *
display_new (DisplayServer *display_server)
{
    Display *display = g_object_new (DISPLAY_TYPE, NULL);

    display->priv->display_server = g_object_ref (display_server);

    return display;
}

DisplayServer *
display_get_display_server (Display *display)
{
    g_return_val_if_fail (display != NULL, NULL);
    return display->priv->display_server;
}

const gchar *
display_get_username (Display *display)
{
    g_return_val_if_fail (display != NULL, NULL);

    if (!display->priv->session || !display->priv->in_user_session)
        return NULL;

    return session_get_username (display->priv->session);
}

Session *
display_get_session (Display *display)
{
    g_return_val_if_fail (display != NULL, NULL);
    return display->priv->session;
}

void
display_set_greeter_session (Display *display, const gchar *greeter_session)
{
    g_return_if_fail (display != NULL);
    g_free (display->priv->greeter_session);
    display->priv->greeter_session = g_strdup (greeter_session);
}

void
display_set_session_wrapper (Display *display, const gchar *session_wrapper)
{
    g_return_if_fail (display != NULL);
    g_free (display->priv->session_wrapper);
    display->priv->session_wrapper = g_strdup (session_wrapper);
}

void
display_set_allow_guest (Display *display, gboolean allow_guest)
{
    g_return_if_fail (display != NULL);
    display->priv->allow_guest = allow_guest;
}

void
display_set_greeter_allow_guest (Display *display, gboolean greeter_allow_guest)
{
    g_return_if_fail (display != NULL);
    display->priv->greeter_allow_guest = greeter_allow_guest;
}

void
display_set_autologin_user (Display *display, const gchar *username, gboolean is_guest, gint timeout)
{
    g_return_if_fail (display != NULL);
    g_free (display->priv->autologin_user);
    display->priv->autologin_user = g_strdup (username);
    display->priv->autologin_guest = is_guest;
    display->priv->autologin_timeout = timeout;
}

void
display_set_select_user_hint (Display *display, const gchar *username, gboolean is_guest)
{
    g_return_if_fail (display != NULL);
    g_free (display->priv->select_user_hint);
    display->priv->select_user_hint = g_strdup (username);
    display->priv->select_guest_hint = is_guest;
}

void
display_set_hide_users_hint (Display *display, gboolean hide_users)
{
    g_return_if_fail (display != NULL);
    display->priv->greeter_hide_users = hide_users;
}

void
display_set_show_manual_login_hint (Display *display, gboolean show_manual_login)
{
    g_return_if_fail (display != NULL);
    display->priv->greeter_show_manual_login = show_manual_login;
}

void
display_set_show_remote_login_hint (Display *display, gboolean show_remote_login)
{
    g_return_if_fail (display != NULL);
    display->priv->greeter_show_remote_login = show_remote_login;
}

void
display_set_lock_hint (Display *display, gboolean is_lock)
{
    g_return_if_fail (display != NULL);
    display->priv->greeter_is_lock = is_lock;
}

void
display_set_user_session (Display *display, SessionType type, const gchar *session_name)
{
    g_return_if_fail (display != NULL);
    g_free (display->priv->user_session);
    display->priv->user_session_type = type;
    display->priv->user_session = g_strdup (session_name);
}

static void
display_set_is_ready (Display *display)
{
    if (display->priv->is_ready)
        return;

    display->priv->is_ready = TRUE;
    g_signal_emit (display, signals[READY], 0);
}

static gboolean
switch_to_user (Display *display, User *user)
{
    gboolean result;
    g_signal_emit (display, signals[SWITCH_TO_USER], 0, user, &result);
    return result;
}

static gboolean
switch_to_guest (Display *display)
{
    gboolean result;
    g_signal_emit (display, signals[SWITCH_TO_GUEST], 0, &result);
    return result;
}

static gchar *
get_guest_username (Display *display)
{
    gchar *username;
    g_signal_emit (display, signals[GET_GUEST_USERNAME], 0, &username);
    return username;
}

static Session *
create_session (Display *display)
{
    Session *session;

    g_signal_emit (display, signals[CREATE_SESSION], 0, &session);
    if (!session)
        return NULL;

    /* Connect using the session bus */
    if (getuid () != 0)
    {
        if (g_getenv ("DBUS_SESSION_BUS_ADDRESS"))
            session_set_env (session, "DBUS_SESSION_BUS_ADDRESS", g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
        session_set_env (session, "LDM_BUS", "SESSION");
        if (g_getenv ("LD_PRELOAD"))
            session_set_env (session, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
        if (g_getenv ("LD_LIBRARY_PATH"))
            session_set_env (session, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
        if (g_getenv ("PATH"))
            session_set_env (session, "PATH", g_getenv ("PATH"));
    }

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

    return session;
}

static void
destroy_session (Display *display)
{
    if (!display->priv->session)
        return;

    g_signal_handlers_disconnect_matched (display->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    session_stop (display->priv->session);
    g_object_unref (display->priv->session);
    display->priv->session = NULL;
}

static void
greeter_authentication_complete_cb (Session *session, Display *display)
{
    gboolean result = FALSE;

    if (display->priv->stopping)
        return;

    if (session_get_is_authenticated (session))
    {
        g_debug ("Greeter authorized");
        g_signal_emit (display, signals[START_GREETER], 0, &result);
        result = !result;
    }
    else
        g_debug ("Greeter failed authentication");

    if (!result)
    {
        g_debug ("Greeter failed to start");
        display_stop (display);
    }
}

static void
greeter_connected_cb (Greeter *greeter, Display *display)
{
    // FIXME: Should wait for greeter to signal completely ready if it supports it
    g_debug ("Greeter connected, display is ready");
    display_set_is_ready (display);
}

static Session *
greeter_start_authentication_cb (Greeter *greeter, const gchar *username, Display *display)
{
    return create_session (display);
}

static gboolean
greeter_start_session_cb (Greeter *greeter, SessionType type, const gchar *session_name, Display *display)
{
    /* If no session requested, use the previous one */
    if (!session_name && !greeter_get_guest_authenticated (greeter))
    {
        User *user;

        user = session_get_user (greeter_get_authentication_session (greeter));
        type = SESSION_TYPE_LOCAL;
        session_name = user_get_xsession (user);
    }

    /* If a session was requested, override the default */
    if (session_name)
    {
        g_debug ("Using session %s", session_name);
        display_set_user_session (display, type, session_name);
    }

    /* Stop this display if that session already exists and can switch to it */
    if (greeter_get_guest_authenticated (greeter))
    {
        if (switch_to_guest (display))
            return TRUE;

        /* Set to login as guest */
        display_set_autologin_user (display, NULL, TRUE, 0);
    }
    else
    {
        if (switch_to_user (display, session_get_user (greeter_get_authentication_session (display->priv->greeter))))
            return TRUE;
    }

    /* Stop the greeter, the session will start when the greeter has quit */
    g_debug ("Stopping greeter");
    session_stop (display->priv->session);

    return TRUE;
}

static gboolean
start_greeter (Display *display)
{
    gchar *greeter_user;
    gboolean result;

    destroy_session (display);
    display->priv->session = create_session (display);
    session_set_class (display->priv->session, XDG_SESSION_CLASS_GREETER);
    g_signal_connect (display->priv->session, "authentication-complete", G_CALLBACK (greeter_authentication_complete_cb), display);

    /* Make communication link to greeter that will run on this session */
    display->priv->greeter = greeter_new (display->priv->session, USER_SERVICE, AUTOLOGIN_SERVICE);
    g_signal_connect (G_OBJECT (display->priv->greeter), "connected", G_CALLBACK (greeter_connected_cb), display);
    g_signal_connect (G_OBJECT (display->priv->greeter), "start-authentication", G_CALLBACK (greeter_start_authentication_cb), display);
    g_signal_connect (G_OBJECT (display->priv->greeter), "start-session", G_CALLBACK (greeter_start_session_cb), display);
    if (display->priv->autologin_timeout)
    {
        gchar *value = g_strdup_printf ("%d", display->priv->autologin_timeout);
        greeter_set_hint (display->priv->greeter, "autologin-timeout", value);
        g_free (value);
        if (display->priv->autologin_user)
            greeter_set_hint (display->priv->greeter, "autologin-user", display->priv->autologin_user);
        else if (display->priv->autologin_guest)
            greeter_set_hint (display->priv->greeter, "autologin-guest", "true");
    }
    if (display->priv->select_user_hint)
        greeter_set_hint (display->priv->greeter, "select-user", display->priv->select_user_hint);
    else if (display->priv->select_guest_hint)
        greeter_set_hint (display->priv->greeter, "select-guest", "true");
    greeter_set_hint (display->priv->greeter, "default-session", display->priv->user_session);
    greeter_set_allow_guest (display->priv->greeter, display->priv->allow_guest);
    greeter_set_hint (display->priv->greeter, "has-guest-account", (display->priv->allow_guest && display->priv->greeter_allow_guest) ? "true" : "false");
    greeter_set_hint (display->priv->greeter, "hide-users", display->priv->greeter_hide_users ? "true" : "false");
    greeter_set_hint (display->priv->greeter, "show-manual-login", display->priv->greeter_show_manual_login ? "true" : "false");
    greeter_set_hint (display->priv->greeter, "show-remote-login", display->priv->greeter_show_remote_login ? "true" : "false");
    if (display->priv->greeter_is_lock)
        greeter_set_hint (display->priv->greeter, "lock-screen", "true");

    /* Run greeter as unprivileged user */
    if (getuid () != 0)
    {
        User *user;
        user = accounts_get_current_user ();
        greeter_user = g_strdup (user_get_name (user));
        g_object_unref (user);
    }
    else
    {
        greeter_user = config_get_string (config_get_instance (), "LightDM", "greeter-user");
        if (!greeter_user)
        {
            g_warning ("Greeter must not be run as root");
            display_stop (display);
            return FALSE;
        }
    }

    g_signal_connect_after (display->priv->session, "stopped", G_CALLBACK (greeter_session_stopped_cb), display);
    result = greeter_start (display->priv->greeter, GREETER_SERVICE, greeter_user);
    g_free (greeter_user);

    if (!result)
        display_stop (display);

    return result;
}

static void
autologin_authentication_complete_cb (Session *session, Display *display)
{
    gboolean result = FALSE;

    if (display->priv->stopping)
        return;

    if (session_get_is_authenticated (session))
    {
        const gchar *session_name;

        g_debug ("Autologin user %s authorized", session_get_username (session));

        session_name = user_get_xsession (session_get_user (session));
        if (session_name)
        {
            g_debug ("Autologin using session %s", session_name);
            display_set_user_session (display, SESSION_TYPE_LOCAL, session_name);
        }

        g_signal_emit (display, signals[START_SESSION], 0, &result);
        result = !result;
    }
    else
        g_debug ("Autologin failed authentication");

    if (!result)
    {
        if (display->priv->start_greeter_if_fail)
            start_greeter (display);
        else
            display_stop (display);
    }
}

static gboolean
autologin (Display *display, const gchar *username, const gchar *service, gboolean start_greeter_if_fail, gboolean is_guest)
{
    display->priv->start_greeter_if_fail = start_greeter_if_fail;

    display->priv->in_user_session = TRUE;
    destroy_session (display);
    display->priv->session = create_session (display);
    g_signal_connect (display->priv->session, "authentication-complete", G_CALLBACK (autologin_authentication_complete_cb), display);
    g_signal_connect_after (display->priv->session, "stopped", G_CALLBACK (user_session_stopped_cb), display);
    return session_start (display->priv->session, service, username, TRUE, FALSE, is_guest);
}

static gboolean
autologin_guest (Display *display, const gchar *service, gboolean start_greeter_if_fail)
{
    gchar *username;
    gboolean result;

    username = get_guest_username (display);
    if (!username)
    {
        g_debug ("Can't autologin guest, no guest account");
        return FALSE;
    }

    result = autologin (display, username, service, start_greeter_if_fail, TRUE);
    g_free (username);

    return result;
}

static gchar **
get_session_command (const gchar *filename, const gchar *session_wrapper)
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

static void
greeter_session_stopped_cb (Session *session, Display *display)
{
    gboolean result = FALSE;

    g_debug ("Greeter quit");

    g_signal_handlers_disconnect_matched (display->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    g_object_unref (display->priv->session);
    display->priv->session = NULL;

    if (display->priv->stopping)
    {
        display_stop (display);
        return;
    }

    if (!display->priv->display_server)
        return;

    /* Start the session for the authenticated user */
    if (greeter_get_start_session (display->priv->greeter))
    {
        /* If guest, then start a new autologin guest session (so can setup account) */
        if (greeter_get_guest_authenticated (display->priv->greeter))
            result = autologin_guest (display, AUTOLOGIN_SERVICE, FALSE);
        /* Otherwise, use the session the greeter has authenticated */
        else
        {
            destroy_session (display);
            display->priv->session = g_object_ref (greeter_get_authentication_session (display->priv->greeter));
            g_signal_connect_after (display->priv->session, "stopped", G_CALLBACK (user_session_stopped_cb), display);
            display->priv->in_user_session = TRUE;
            g_signal_emit (display, signals[START_SESSION], 0, &result);
            result = !result;
        }
    }

    /* Destroy the greeter */
    g_signal_handlers_disconnect_matched (display->priv->greeter, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    g_object_unref (display->priv->greeter);
    display->priv->greeter = NULL;

    if (!result)
    {
        g_debug ("Failed to start greeter");
        display_stop (display);
    }
}

static gboolean
display_start_greeter (Display *display)
{
    gchar *log_dir, *filename, *log_filename, *sessions_dir, *path;
    gchar **argv;

    /* Log the output of the greeter to a system location */
    log_dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    filename = g_strdup_printf ("%s-greeter.log", display_server_get_name (display->priv->display_server));
    log_filename = g_build_filename (log_dir, filename, NULL);
    g_free (log_dir);
    g_free (filename);
    g_debug ("Logging to %s", log_filename);
    session_set_log_file (display->priv->session, log_filename);
    g_free (log_filename);

    /* Load the greeter session information */
    sessions_dir = config_get_string (config_get_instance (), "LightDM", "xgreeters-directory");
    filename = g_strdup_printf ("%s.desktop", display->priv->greeter_session);
    path = g_build_filename (sessions_dir, filename, NULL);
    g_free (sessions_dir);
    g_free (filename);
    argv = get_session_command (path, NULL);
    g_free (path);
    if (!argv)
        return TRUE;

    session_run (display->priv->session, argv);

    return FALSE;
}

static void
user_session_stopped_cb (Session *session, Display *display)
{
    g_debug ("User session quit");

    g_signal_handlers_disconnect_matched (display->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    g_object_unref (display->priv->session);
    display->priv->session = NULL;

    /* This display has ended */
    display_stop (display);
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

static gboolean
display_start_session (Display *display)
{
    User *user;
    gchar *filename, *sessions_dir, *path;
    gchar **argv;

    user = session_get_user (display->priv->session);

    /* Find the command to run for the selected session */
    if (display->priv->user_session_type == SESSION_TYPE_LOCAL)
    {
        sessions_dir = config_get_string (config_get_instance (), "LightDM", "xsessions-directory");

        /* Store this session name so we automatically use it next time */
        user_set_xsession (user, display->priv->user_session);
    }
    else
        sessions_dir = config_get_string (config_get_instance (), "LightDM", "remote-sessions-directory");
    filename = g_strdup_printf ("%s.desktop", display->priv->user_session);
    path = g_build_filename (sessions_dir, filename, NULL);
    g_free (sessions_dir);
    g_free (filename);
    argv = get_session_command (path, display->priv->session_wrapper);
    g_free (path);
    if (!argv)
        return TRUE;
  
    session_set_env (display->priv->session, "DESKTOP_SESSION", display->priv->user_session); // FIXME: Apparently deprecated?
    session_set_env (display->priv->session, "GDMSESSION", display->priv->user_session); // FIXME: Not cross-desktop

    /* Run a guest session through the wrapper covered by MAC */
    if (display->priv->autologin_guest)
    {
        gchar *wrapper = g_build_filename (PKGLIBEXEC_DIR, "lightdm-guest-session-wrapper", NULL);
        g_debug ("Running guest session through wrapper: %s", wrapper);
        prepend_argv (&argv, wrapper);
        g_free (wrapper);
    }

    g_debug ("Starting session %s as user %s", display->priv->user_session, session_get_username (display->priv->session));

    session_run (display->priv->session, argv);
    g_strfreev (argv);

    // FIXME: Wait for session to indicate it is ready (maybe)
    display_set_is_ready (display);

    return FALSE;
}

static void
display_server_stopped_cb (DisplayServer *server, Display *display)
{
    g_debug ("Display server stopped");

    g_signal_handlers_disconnect_matched (display->priv->display_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    g_object_unref (display->priv->display_server);
    display->priv->display_server = NULL;

    /* Stop this display, it will be restarted by the seat if necessary */
    display_stop (display);
}

static void
display_server_ready_cb (DisplayServer *display_server, Display *display)
{
    gboolean result = FALSE;

    g_signal_emit (display, signals[DISPLAY_SERVER_READY], 0, &result);
    if (!result)
    {
        display_stop (display);
        return;
    }

    /* Don't run any sessions on local terminals */
    if (!display_server_get_start_local_sessions (display_server))
        return;

    /* Automatically start requested user session */
    result = FALSE;
    if (display->priv->autologin_timeout == 0 && display->priv->autologin_guest)
    {
        g_debug ("Automatically logging in as guest");
        result = autologin_guest (display, AUTOLOGIN_SERVICE, TRUE);
    }
    else if (display->priv->autologin_timeout == 0 && display->priv->autologin_user)
    {
        g_debug ("Automatically logging in user %s", display->priv->autologin_user);
        result = autologin (display, display->priv->autologin_user, AUTOLOGIN_SERVICE, TRUE, FALSE);
    }
    else if (display->priv->select_user_hint)
    {
        g_debug ("Logging in user %s", display->priv->select_user_hint);
        result = autologin (display, display->priv->select_user_hint, USER_SERVICE, TRUE, FALSE);
    }

    /* If no session started, start a greeter */
    if (!result)
    {
        g_debug ("Starting greeter");      
        result = start_greeter (display);
    }

    /* If nothing started, then the display can't work */
    if (!result)
        display_stop (display);
}

gboolean
display_start (Display *display)
{
    g_return_val_if_fail (display != NULL, FALSE);

    g_signal_connect (G_OBJECT (display->priv->display_server), "ready", G_CALLBACK (display_server_ready_cb), display);
    g_signal_connect (G_OBJECT (display->priv->display_server), "stopped", G_CALLBACK (display_server_stopped_cb), display);

    if (!display_server_start (display->priv->display_server))
        return FALSE;

    return TRUE;
}

void
display_stop (Display *display)
{
    g_return_if_fail (display != NULL);

    if (display->priv->stopped)
        return;

    if (!display->priv->stopping)
    {
        g_debug ("Stopping display");
        display->priv->stopping = TRUE;
    }

    /* Stop the session first */
    if (display->priv->session)
    {
        session_stop (display->priv->session);
        if (display->priv->session && !session_get_is_stopped (display->priv->session))
            return;
        g_signal_handlers_disconnect_matched (display->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        g_object_unref (display->priv->session);
        display->priv->session = NULL;
    }

    /* Stop the display server after that */
    if (display->priv->display_server)
    {
        display_server_stop (display->priv->display_server);
        if (display->priv->display_server && !display_server_get_is_stopped (display->priv->display_server))
            return;
        g_signal_handlers_disconnect_matched (display->priv->display_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        g_object_unref (display->priv->display_server);
        display->priv->display_server = NULL;
    }

    display->priv->stopped = TRUE;
    g_debug ("Display stopped");
    g_signal_emit (display, signals[STOPPED], 0);
}

gboolean
display_get_is_ready (Display *display)
{
    g_return_val_if_fail (display != NULL, FALSE);

    return display->priv->is_ready;
}

void
display_lock (Display *display)
{
    g_return_if_fail (display != NULL);

    if (!display->priv->session)
        return;

    g_debug ("Locking display");

    session_lock (display->priv->session);
}

void
display_unlock (Display *display)
{
    g_return_if_fail (display != NULL);

    if (!display->priv->session)
        return;

    g_debug ("Unlocking display");

    session_unlock (display->priv->session);
}

static gboolean
display_real_switch_to_user (Display *display, User *user)
{
    return FALSE;
}

static gboolean
display_real_switch_to_guest (Display *display)
{
    return FALSE;
}

static gchar *
display_real_get_guest_username (Display *display)
{
    return NULL;
}

static void
display_init (Display *display)
{
    display->priv = G_TYPE_INSTANCE_GET_PRIVATE (display, DISPLAY_TYPE, DisplayPrivate);
}

static void
display_finalize (GObject *object)
{
    Display *self;

    self = DISPLAY (object);

    if (self->priv->display_server)
    {
        g_signal_handlers_disconnect_matched (self->priv->display_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
        g_object_unref (self->priv->display_server);
    }
    g_free (self->priv->greeter_session);
    if (self->priv->greeter)
    {
        g_signal_handlers_disconnect_matched (self->priv->greeter, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
        g_object_unref (self->priv->greeter);
    }
    g_free (self->priv->session_wrapper);
    if (self->priv->session)
    {
        g_signal_handlers_disconnect_matched (self->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);      
        g_object_unref (self->priv->session);
    }
    g_free (self->priv->autologin_user);
    g_free (self->priv->select_user_hint);
    g_free (self->priv->user_session);

    G_OBJECT_CLASS (display_parent_class)->finalize (object);
}

static void
display_class_init (DisplayClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->switch_to_user = display_real_switch_to_user;
    klass->switch_to_guest = display_real_switch_to_guest;
    klass->get_guest_username = display_real_get_guest_username;
    klass->start_greeter = display_start_greeter;
    klass->start_session = display_start_session;
    object_class->finalize = display_finalize;

    g_type_class_add_private (klass, sizeof (DisplayPrivate));

    signals[CREATE_SESSION] =
        g_signal_new ("create-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, create_session),
                      NULL, NULL,
                      ldm_marshal_OBJECT__VOID,
                      SESSION_TYPE, 0);
    signals[READY] =
        g_signal_new ("ready",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, ready),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
    signals[SWITCH_TO_USER] =
        g_signal_new ("switch-to-user",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, switch_to_user),
                      g_signal_accumulator_true_handled,
                      NULL,
                      ldm_marshal_BOOLEAN__OBJECT,
                      G_TYPE_BOOLEAN, 1, USER_TYPE);
    signals[SWITCH_TO_GUEST] =
        g_signal_new ("switch-to-guest",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, switch_to_guest),
                      g_signal_accumulator_true_handled,
                      NULL,
                      ldm_marshal_BOOLEAN__VOID,
                      G_TYPE_BOOLEAN, 0);
    signals[GET_GUEST_USERNAME] =
        g_signal_new ("get-guest-username",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, get_guest_username),
                      g_signal_accumulator_first_wins,
                      NULL,
                      ldm_marshal_STRING__VOID,
                      G_TYPE_STRING, 0);
    signals[DISPLAY_SERVER_READY] =
        g_signal_new ("display-server-ready",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, display_server_ready),
                      NULL, NULL,
                      ldm_marshal_BOOLEAN__VOID,
                      G_TYPE_BOOLEAN, 0);
    signals[START_GREETER] =
        g_signal_new ("start-greeter",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, start_greeter),
                      g_signal_accumulator_true_handled, NULL,
                      ldm_marshal_BOOLEAN__VOID,
                      G_TYPE_BOOLEAN, 0);
    signals[START_SESSION] =
        g_signal_new ("start-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, start_session),
                      g_signal_accumulator_true_handled, NULL,
                      ldm_marshal_BOOLEAN__VOID,
                      G_TYPE_BOOLEAN, 0);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
