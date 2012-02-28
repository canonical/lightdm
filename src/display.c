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
#include "pam-session.h"
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

typedef enum
{
    SESSION_NONE = 0,
    SESSION_GREETER_PRE_CONNECT,
    SESSION_GREETER,
    SESSION_GREETER_AUTHENTICATED,
    SESSION_USER
} SessionType;

struct DisplayPrivate
{
    /* Display server */
    DisplayServer *display_server;

    /* Greeter session */
    gchar *greeter_session;

    /* TRUE if the user list should be shown */
    gboolean greeter_hide_users;

    /* TRUE if the greeter is a lock screen */
    gboolean greeter_is_lock;

    /* Session requested to log into */
    gchar *user_session;

    /* Program to run sessions through */
    gchar *session_wrapper;

    /* PAM service to authenticate against */
    gchar *pam_service;

    /* PAM service to authenticate against for automatic logins */
    gchar *pam_autologin_service;
  
    /* TRUE if a session should be started on greeter quit */
    gboolean start_session_on_greeter_quit;

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

    /* TRUE if stopping the display (waiting for dispaly server, greeter and session to stop) */
    gboolean stopping;

    /* TRUE if stopped */
    gboolean stopped;
};

G_DEFINE_TYPE (Display, display, G_TYPE_OBJECT);

static gboolean start_greeter_session (Display *display);
static gboolean start_user_session (Display *display, PAMSession *authentication);

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

    return user_get_name (session_get_user (display->priv->session));
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
display_set_lock_hint (Display *display, gboolean is_lock)
{
    g_return_if_fail (display != NULL);
    display->priv->greeter_is_lock = is_lock;
}

void
display_set_user_session (Display *display, const gchar *session_name)
{
    g_return_if_fail (display != NULL);
    g_free (display->priv->user_session);
    display->priv->user_session = g_strdup (session_name);
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

static void
autologin_authentication_result_cb (PAMSession *authentication, int result, Display *display)
{
    g_signal_handlers_disconnect_matched (authentication, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    if (display->priv->stopping)
        return;

    gboolean started_session = FALSE;

    if (result == PAM_SUCCESS)
    {
        User *user;
        const gchar *session_name;

        user = pam_session_get_user (authentication);

        if (user)
        {
            g_debug ("User %s authorized", pam_session_get_username (authentication));

            session_name = user_get_xsession (user);
            if (session_name)
            {
                g_debug ("Using session %s", session_name);
                display_set_user_session (display, session_name);
            }

            started_session = start_user_session (display, authentication);
            if (!started_session)
                g_debug ("Failed to start autologin session");
        }
        else
            g_debug ("User %s was authorized, but no account of that name exists", pam_session_get_username (authentication));
    }
    else
        g_debug ("Autologin failed authentication");

    if (!started_session && display->priv->start_greeter_if_fail)
    {
        display_set_autologin_user (display, NULL, FALSE, 0);
        if (display->priv->autologin_user)
            display_set_select_user_hint (display, display->priv->autologin_user, FALSE);
        started_session = start_greeter_session (display);
    }

    if (!started_session)
        display_stop (display);
}

static gboolean
autologin (Display *display, const gchar *username, const gchar *service, gboolean start_greeter_if_fail)
{
    gboolean result;
    PAMSession *authentication;
    GError *error = NULL;

    display->priv->start_greeter_if_fail = start_greeter_if_fail;

    display->priv->in_user_session = TRUE;
    authentication = pam_session_new (service, username);
    pam_session_set_interactive (authentication, FALSE);
    g_signal_connect (authentication, "authentication-result", G_CALLBACK (autologin_authentication_result_cb), display);

    result = pam_session_authenticate (authentication, &error);
    if (error)
        g_debug ("Failed to start autologin session for %s: %s", username, error->message);
    if (!result)
    {
        g_signal_handlers_disconnect_matched (authentication, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        g_object_unref (authentication);
    }
    g_clear_error (&error);

    return result;
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

    result = autologin (display, username, service, start_greeter_if_fail);
    g_free (username);

    return result;
}

static gboolean
cleanup_after_session (Display *display)
{
    g_signal_handlers_disconnect_matched (display->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    g_object_unref (display->priv->session);
    display->priv->session = NULL;

    if (display->priv->stopping)
    {
        display_stop (display);
        return TRUE;
    }

    return FALSE;
}

static void
greeter_session_stopped_cb (Session *session, Display *display)
{
    gboolean started_session = FALSE;

    g_debug ("Greeter quit");

    if (cleanup_after_session (display))
        return;

    if (!display->priv->display_server)
        return;

    /* Start the session for the authenticated user */
    if (display->priv->start_session_on_greeter_quit)
    {
        if (greeter_get_guest_authenticated (display->priv->greeter))
        {
            started_session = autologin_guest (display, display->priv->pam_autologin_service, FALSE);
            if (!started_session)
                g_debug ("Failed to start guest session");
        }
        else
        {
            display->priv->in_user_session = TRUE;
            started_session = start_user_session (display, greeter_get_authentication (display->priv->greeter));
            if (!started_session)
                g_debug ("Failed to start user session");
        }
    }

    g_signal_handlers_disconnect_matched (display->priv->greeter, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    g_object_unref (display->priv->greeter);
    display->priv->greeter = NULL;

    if (!started_session)
        display_stop (display);
}

static void
user_session_stopped_cb (Session *session, Display *display)
{
    g_debug ("User session quit");

    if (cleanup_after_session (display))
        return;

    /* This display has ended */
    display_stop (display);
}

static Session *
create_session (Display *display, PAMSession *authentication, const gchar *session_name, gboolean is_greeter)
{
    gchar *sessions_dir, *filename, *path, *command = NULL;
    GKeyFile *session_desktop_file;
    gint argc;
    gchar **argv;
    Session *session;
    gboolean result;
    GError *error = NULL;

    g_debug ("Starting session %s as user %s", session_name, pam_session_get_username (authentication));

    // FIXME: This is X specific, move into xsession.c
    if (is_greeter)
        sessions_dir = config_get_string (config_get_instance (), "LightDM", "xgreeters-directory");
    else
        sessions_dir = config_get_string (config_get_instance (), "LightDM", "xsessions-directory");
    filename = g_strdup_printf ("%s.desktop", session_name);
    path = g_build_filename (sessions_dir, filename, NULL);
    g_free (sessions_dir);
    g_free (filename);

    session_desktop_file = g_key_file_new ();
    result = g_key_file_load_from_file (session_desktop_file, path, G_KEY_FILE_NONE, &error);
    if (error)
        g_debug ("Failed to load session file %s: %s:", path, error->message);
    g_clear_error (&error);
    if (result)
    {
        command = g_key_file_get_string (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
        if (!command)
            g_debug ("No command in session file %s", path);
    }
    g_key_file_free (session_desktop_file);
    g_free (path);
    if (!command)
        return NULL;

    result = g_shell_parse_argv (command, &argc, &argv, &error);
    if (error)
        g_debug ("Invalid session command '%s': %s", command, error->message);
    g_clear_error (&error);
    g_free (command);
    if (!result)
        return NULL;

    /* Convert to full path */
    path = g_find_program_in_path (argv[0]);
    if (path)
    {
        g_free (argv[0]);
        argv[0] = path;
    }
    command = g_strjoinv (" ", argv);

    if (display->priv->session_wrapper && !is_greeter)
    {
        gchar *wrapper;

        wrapper = g_find_program_in_path (display->priv->session_wrapper);
        if (wrapper)
        {
            gchar *t = command;
            command = g_strdup_printf ("%s '%s'", wrapper, command);
            g_free (t);
            g_free (wrapper);
        }
    }

    /* for a guest session, run command through the wrapper covered by MAC */
    if (display->priv->autologin_guest)
    {
        gchar *t = command;
        command = g_strdup_printf (PKGLIBEXEC_DIR "/lightdm-guest-session-wrapper %s", command);
        g_debug("Guest session, running session command through wrapper: %s", command);
        g_free (t);
    }

    g_signal_emit (display, signals[CREATE_SESSION], 0, &session);
    g_return_val_if_fail (session != NULL, NULL);

    if (is_greeter)
        g_signal_connect_after (session, "stopped", G_CALLBACK (greeter_session_stopped_cb), display);
    else
        g_signal_connect_after (session, "stopped", G_CALLBACK (user_session_stopped_cb), display);
    session_set_is_greeter (session, is_greeter);
    session_set_authentication (session, authentication);
    session_set_command (session, command);
    g_free (command);

    session_set_env (session, "DESKTOP_SESSION", session_name); // FIXME: Apparently deprecated?
    session_set_env (session, "GDMSESSION", session_name); // FIXME: Not cross-desktop

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
    if (g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"))
    {
        session_set_env (session, "LIGHTDM_TEST_STATUS_SOCKET", g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"));
        session_set_env (session, "LIGHTDM_TEST_CONFIG", g_getenv ("LIGHTDM_TEST_CONFIG"));
        session_set_env (session, "LIGHTDM_TEST_HOME_DIR", g_getenv ("LIGHTDM_TEST_HOME_DIR"));
        session_set_env (session, "LIGHTDM_TEST_PASSWD_FILE", g_getenv ("LIGHTDM_TEST_PASSWD_FILE"));
        session_set_env (session, "DBUS_SYSTEM_BUS_ADDRESS", g_getenv ("DBUS_SYSTEM_BUS_ADDRESS"));
        session_set_env (session, "DBUS_SESSION_BUS_ADDRESS", g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
        session_set_env (session, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
        session_set_env (session, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
        session_set_env (session, "GI_TYPELIB_PATH", g_getenv ("GI_TYPELIB_PATH"));
    }

    return session;
}

static void
display_set_is_ready (Display *display)
{
    if (display->priv->is_ready)
        return;

    display->priv->is_ready = TRUE;
    g_signal_emit (display, signals[READY], 0);
}

static void
greeter_connected_cb (Greeter *greeter, Display *display)
{
    // FIXME: Should wait for greeter to signal completely ready if it supports it
    g_debug ("Greeter connected, display is ready");
    display_set_is_ready (display);
}

static PAMSession *
greeter_start_authentication_cb (Greeter *greeter, const gchar *username, Display *display)
{
    return pam_session_new (display->priv->pam_service, username);
}

static gboolean
greeter_start_session_cb (Greeter *greeter, const gchar *session_name, Display *display)
{
    /* If no session requested, use the previous one */
    if (!session_name && !greeter_get_guest_authenticated (greeter))
    {
        User *user;

        user = pam_session_get_user (greeter_get_authentication (greeter));
        session_name = user_get_xsession (user);
    }

    /* If a session was requested, override the default */
    if (session_name)
    {
        g_debug ("Using session %s", session_name);
        display_set_user_session (display, session_name);
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
       if (switch_to_user (display, pam_session_get_user (greeter_get_authentication (display->priv->greeter))))
           return TRUE;
    }

    /* Stop the greeter, the session will start when the greeter has quit */
    g_debug ("Stopping greeter");
    display->priv->start_session_on_greeter_quit = TRUE;
    session_stop (display->priv->session);

    return TRUE;
}

static gboolean
start_greeter_session (Display *display)
{
    User *user;
    gchar *log_dir, *filename, *log_filename;
    PAMSession *authentication;
    gboolean start_result;

    g_return_val_if_fail (display->priv->session == NULL, FALSE);
    g_return_val_if_fail (display->priv->greeter == NULL, FALSE);

    g_debug ("Starting greeter session");

    if (getuid () != 0)
        user = accounts_get_current_user ();
    else
    {
        gchar *greeter_user;

        greeter_user = config_get_string (config_get_instance (), "LightDM", "greeter-user");
        if (!greeter_user)
        {
            g_warning ("Greeter must not be run as root");
            return FALSE;
        }

        user = accounts_get_user_by_name (greeter_user);
        if (!user)
            g_debug ("Unable to start greeter, user %s does not exist", greeter_user);
        g_free (greeter_user);
        if (!user)
            return FALSE;
    }
    display->priv->in_user_session = FALSE;

    /* Authenticate as the requested user */
    authentication = pam_session_new (display->priv->pam_service, user_get_name (user));
    g_object_unref (user);

    display->priv->session = create_session (display, authentication, display->priv->greeter_session, TRUE);
    g_object_unref (authentication);
  
    if (!display->priv->session)
        return FALSE;

    log_dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    filename = g_strdup_printf ("%s-greeter.log", display_server_get_name (display->priv->display_server));
    log_filename = g_build_filename (log_dir, filename, NULL);
    g_free (log_dir);
    g_free (filename);

    g_debug ("Logging to %s", log_filename);
    session_set_log_file (display->priv->session, log_filename, FALSE);
    g_free (log_filename);

    display->priv->greeter = greeter_new (display->priv->session);
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
    greeter_set_hint (display->priv->greeter, "has-guest-account", display->priv->allow_guest ? "true" : "false");
    greeter_set_hint (display->priv->greeter, "hide-users", display->priv->greeter_hide_users ? "true" : "false");
    if (display->priv->greeter_is_lock)
        greeter_set_hint (display->priv->greeter, "lock-screen", "true");

    start_result = FALSE;
    g_signal_emit (display, signals[START_GREETER], 0, &start_result);

    return !start_result;
}

static gboolean
display_start_greeter (Display *display)
{
    if (!greeter_start (display->priv->greeter))
    {
        g_debug ("Failed to start greeter protocol");

        g_signal_handlers_disconnect_matched (display->priv->greeter, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        g_object_unref (display->priv->greeter);
        display->priv->greeter = NULL;

        return TRUE;
    }
  
    if (!session_start (SESSION (display->priv->session)))
    {     
        g_debug ("Failed to start greeter session");
        return TRUE;
    }

    return FALSE;
}

static gboolean
start_user_session (Display *display, PAMSession *authentication)
{
    User *user;
    gchar *log_filename;
    gboolean result;

    g_return_val_if_fail (display->priv->session == NULL, FALSE);

    g_debug ("Starting user session");

    user = pam_session_get_user (authentication);

    /* Update user's xsession setting */
    user_set_xsession (user, display->priv->user_session);
 
    display->priv->session = create_session (display, authentication, display->priv->user_session, FALSE);

    if (!display->priv->session)
        return FALSE;

    // FIXME: Copy old error file
    log_filename = g_build_filename (user_get_home_directory (user), ".xsession-errors", NULL);
  
    /* Delete existing log file if it exists - a bug in 1.0.0 would cause this file to be written as root */
    unlink (log_filename);

    g_debug ("Logging to %s", log_filename);    
    session_set_log_file (display->priv->session, log_filename, TRUE);
    g_free (log_filename);

    g_signal_emit (display, signals[START_SESSION], 0, &result);
    result = !result;

    if (!result)
    {
        g_object_unref (display->priv->session);
        display->priv->session = NULL;
    }

    return result;
}

static gboolean
display_start_session (Display *display)
{
    if (!session_start (SESSION (display->priv->session)))
        return TRUE;

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
    gboolean result;
    gboolean started_session = FALSE;

    g_signal_emit (display, signals[DISPLAY_SERVER_READY], 0, &result);
    if (!result)
    {
        display_stop (display);
        return;
    }

    /* Don't run any sessions on local terminals */
    if (!display_server_get_start_local_sessions (display_server))
        return;

    /* Automatically log in */
    if (display->priv->autologin_guest)
    {
        g_debug ("Automatically logging in as guest");
        started_session = autologin_guest (display, display->priv->pam_autologin_service, TRUE);
        if (!started_session)
            g_debug ("Failed to autologin as guest");
    }
    else if (display->priv->autologin_user)
    {
        g_debug ("Automatically logging in user %s", display->priv->autologin_user);
        started_session = autologin (display, display->priv->autologin_user, display->priv->pam_autologin_service, TRUE);
        if (!started_session)
            g_debug ("Failed to autologin user %s", display->priv->autologin_user);
    }
    else if (display->priv->select_user_hint)
    {
        g_debug ("Logging in user %s", display->priv->select_user_hint);
        started_session = autologin (display, display->priv->select_user_hint, display->priv->pam_service, TRUE);
        if (!started_session)
            g_debug ("Failed to login user %s", display->priv->select_user_hint);
    }
    else if (display->priv->select_guest_hint)
    {
        g_debug ("Logging in as guest");
        started_session = autologin_guest (display, display->priv->pam_service, TRUE);
        if (!started_session)
            g_debug ("Failed login as guest");
    }

    /* Finally start a greeter */
    if (!started_session)
    {
        started_session = start_greeter_session (display);
        if (!started_session)
            g_debug ("Failed to start greeter");
    }

    if (!started_session)
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
    display->priv->pam_service = g_strdup ("lightdm");
    display->priv->pam_autologin_service = g_strdup ("lightdm-autologin");
}

static void
display_finalize (GObject *object)
{
    Display *self;

    self = DISPLAY (object);

    if (self->priv->display_server)
        g_object_unref (self->priv->display_server);
    g_free (self->priv->greeter_session);
    if (self->priv->greeter)
        g_object_unref (self->priv->greeter);
    g_free (self->priv->session_wrapper);
    g_free (self->priv->pam_service);
    g_free (self->priv->pam_autologin_service);
    if (self->priv->session)
        g_object_unref (self->priv->session);
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
