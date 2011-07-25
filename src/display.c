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

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gdesktopappinfo.h>

#include "display.h"
#include "configuration.h"
#include "user.h"
#include "pam-session.h"
#include "dmrc.h"
#include "ldm-marshal.h"
#include "greeter.h"
#include "xserver-local.h" // FIXME: Shouldn't know if it's an xserver

enum {
    STARTED,
    READY,
    SWITCH_TO_USER,
    SWITCH_TO_GUEST,
    GET_GUEST_USERNAME,
    SESSION_STARTED,
    SESSION_STOPPED,
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

    /* User to run greeter as */
    gchar *greeter_user;

    /* Greeter session */
    gchar *greeter_session;

    /* TRUE if the user list should be shown */
    gboolean greeter_hide_users;

    /* Session requested to log into */
    gchar *user_session;
  
    /* Directory to load X sessions from */
    gchar *xsessions_dir;

    /* Directory to load X greeters from */
    gchar *xgreeters_dir;

    /* Program to run sessions through */
    gchar *session_wrapper;

    /* PAM service to authenticate against */
    gchar *pam_service;

    /* PAM service to authenticate against for automatic logins */
    gchar *pam_autologin_service;

    /* TRUE if in a user session */
    gboolean in_user_session;
  
    /* TRUE if have emitted ready signal */
    gboolean indicated_ready;
  
    /* Session process */
    Session *session;

    /* Communication link to greeter */
    Greeter *greeter;

    /* Current PAM session */
    PAMSession *pam_session;

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
};

G_DEFINE_TYPE (Display, display, G_TYPE_OBJECT);

static gboolean start_greeter_session (Display *display);
static gboolean start_user_session (Display *display);

// FIXME: Should be a construct property
// FIXME: Move into seat.c
void
display_load_config (Display *display, const gchar *config_section)
{
    g_return_if_fail (display != NULL);
    
    display->priv->greeter_user = config_get_string (config_get_instance (), "LightDM", "greeter-user");

    if (config_section)
        display->priv->greeter_session = config_get_string (config_get_instance (), config_section, "greeter-session");
    if (!display->priv->greeter_session)
        display->priv->greeter_session = config_get_string (config_get_instance (), "SeatDefaults", "greeter-session");
    if (config_section && config_has_key (config_get_instance (), config_section, "greeter-hide-users"))
        display->priv->greeter_hide_users = config_get_boolean (config_get_instance (), config_section, "greeter-hide-users");
    else if (config_has_key (config_get_instance (), "SeatDefaults", "greeter-hide-users"))
        display->priv->greeter_hide_users = config_get_boolean (config_get_instance (), "SeatDefaults", "greeter-hide-users");
    if (config_section)
        display->priv->user_session = config_get_string (config_get_instance (), config_section, "user-session");
    if (!display->priv->user_session)
        display->priv->user_session = config_get_string (config_get_instance (), "SeatDefaults", "user-session");
    if (config_section)
        display->priv->xsessions_dir = config_get_string (config_get_instance (), config_section, "xsessions-directory");
    if (!display->priv->xsessions_dir)
        display->priv->xsessions_dir = config_get_string (config_get_instance (), "SeatDefaults", "xsessions-directory");
    if (config_section)
        display->priv->xgreeters_dir = config_get_string (config_get_instance (), config_section, "xgreeters-directory");
    if (!display->priv->xgreeters_dir)
        display->priv->xgreeters_dir = config_get_string (config_get_instance (), "SeatDefaults", "xgreeters-directory");
    if (config_section)
        display->priv->session_wrapper = config_get_string (config_get_instance (), config_section, "session-wrapper");
    if (!display->priv->session_wrapper)
        display->priv->session_wrapper = config_get_string (config_get_instance (), "SeatDefaults", "session-wrapper");
}

// FIXME: Should be a construct property
void
display_set_display_server (Display *display, DisplayServer *display_server)
{
    g_return_if_fail (display != NULL);
    g_return_if_fail (display->priv->display_server == NULL);
    display->priv->display_server = g_object_ref (display_server);
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

    if (display->priv->pam_session && display->priv->in_user_session)
        return pam_session_get_username (display->priv->pam_session);
    else
        return NULL;
}

Session *
display_get_session (Display *display)
{
    g_return_val_if_fail (display != NULL, NULL);
    return display->priv->session;
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
display_set_user_session (Display *display, const gchar *session_name)
{
    g_return_if_fail (display != NULL);
    if (session_name)
    {
        g_free (display->priv->user_session);
        display->priv->user_session = g_strdup (session_name);
    }
}

static gboolean
switch_to_user (Display *display, const gchar *username)
{
    gboolean result;
    g_signal_emit (display, signals[SWITCH_TO_USER], 0, username, &result);
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

static gchar *
start_ck_session (Display *display, const gchar *session_type, User *user)
{
    GDBusProxy *proxy;
    const gchar *hostname = "";
    GVariantBuilder arg_builder;
    GVariant *result;
    gchar *cookie = NULL;
    GError *error = NULL;

    /* Only start ConsoleKit sessions when running as root */
    if (getuid () != 0)
        return NULL;

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.ConsoleKit",
                                           "/org/freedesktop/ConsoleKit/Manager",
                                           "org.freedesktop.ConsoleKit.Manager", 
                                           NULL, &error);
    if (!proxy)
        g_warning ("Unable to get connection to ConsoleKit: %s", error->message);
    g_clear_error (&error);
    if (!proxy)
        return NULL;

    g_variant_builder_init (&arg_builder, G_VARIANT_TYPE ("(a(sv))"));
    g_variant_builder_open (&arg_builder, G_VARIANT_TYPE ("a(sv)"));
    g_variant_builder_add (&arg_builder, "(sv)", "unix-user", g_variant_new_int32 (user_get_uid (user)));
    g_variant_builder_add (&arg_builder, "(sv)", "session-type", g_variant_new_string (session_type));
    if (IS_XSERVER (display->priv->display_server))
    {
        g_variant_builder_add (&arg_builder, "(sv)", "x11-display",
                               g_variant_new_string (xserver_get_address (XSERVER (display->priv->display_server))));

        if (IS_XSERVER_LOCAL (display->priv->display_server) && xserver_local_get_vt (XSERVER_LOCAL (display->priv->display_server)) >= 0)
        {
            gchar *display_device;
            display_device = g_strdup_printf ("/dev/tty%d", xserver_local_get_vt (XSERVER_LOCAL (display->priv->display_server)));
            g_variant_builder_add (&arg_builder, "(sv)", "x11-display-device", g_variant_new_string (display_device));
            g_free (display_device);
        }
    }

    g_variant_builder_add (&arg_builder, "(sv)", "remote-host-name", g_variant_new_string (hostname));
    g_variant_builder_add (&arg_builder, "(sv)", "is-local", g_variant_new_boolean (TRUE));
    g_variant_builder_close (&arg_builder);

    result = g_dbus_proxy_call_sync (proxy,
                                     "OpenSessionWithParameters",
                                     g_variant_builder_end (&arg_builder),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    g_object_unref (proxy);

    if (!result)
        g_warning ("Failed to open CK session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return NULL;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(s)")))
        g_variant_get (result, "(s)", &cookie);
    else
        g_warning ("Unexpected response from OpenSessionWithParameters: %s", g_variant_get_type_string (result));
    g_variant_unref (result);

    if (cookie)
        g_debug ("Opened ConsoleKit session %s", cookie);

    return cookie;
}

static void
end_ck_session (const gchar *cookie)
{
    GDBusProxy *proxy;
    GVariant *result;
    GError *error = NULL;

    if (!cookie)
        return;

    g_debug ("Ending ConsoleKit session %s", cookie);

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.ConsoleKit",
                                           "/org/freedesktop/ConsoleKit/Manager",
                                           "org.freedesktop.ConsoleKit.Manager", 
                                           NULL, NULL);
    result = g_dbus_proxy_call_sync (proxy,
                                     "CloseSession",
                                     g_variant_new ("(s)", cookie),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    g_object_unref (proxy);

    if (!result)
        g_warning ("Error ending ConsoleKit session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(b)")))
    {
        gboolean is_closed;
        g_variant_get (result, "(b)", &is_closed);
        if (!is_closed)
            g_warning ("ConsoleKit.Manager.CloseSession() returned false");
    }
    else
        g_warning ("Unexpected response from CloseSession: %s", g_variant_get_type_string (result));

    g_variant_unref (result);
}

static void
set_env_from_pam_session (Session *session, PAMSession *pam_session)
{
    gchar **pam_env;

    pam_env = pam_session_get_envlist (pam_session);
    if (pam_env)
    {
        gchar *env_string;      
        int i;

        env_string = g_strjoinv (" ", pam_env);
        g_debug ("PAM returns environment '%s'", env_string);
        g_free (env_string);

        for (i = 0; pam_env[i]; i++)
        {
            gchar **pam_env_vars = g_strsplit (pam_env[i], "=", 2);
            if (pam_env_vars && pam_env_vars[0] && pam_env_vars[1])
                process_set_env (PROCESS (session), pam_env_vars[0], pam_env_vars[1]);
            else
                g_warning ("Can't parse PAM environment variable %s", pam_env[i]);
            g_strfreev (pam_env_vars);
        }
        g_strfreev (pam_env);
    }
}

static void
session_exited_cb (Session *session, gint status, Display *display)
{
    if (status != 0)
        g_debug ("Session exited with value %d", status);
}

static void
session_terminated_cb (Session *session, gint signum, Display *display)
{
    g_debug ("Session terminated with signal %d", signum);
}

static void
check_stopped (Display *display)
{
    if (display->priv->stopping &&
        display->priv->display_server == NULL &&
        display->priv->session == NULL)
    {
        g_debug ("Display stopped");
        g_signal_emit (display, signals[STOPPED], 0);
    }
}

static void
autologin_pam_message_cb (PAMSession *session, int num_msg, const struct pam_message **msg, Display *display)
{
    g_debug ("Aborting automatic login as PAM requests input");
    pam_session_stop (session);
}

static void
autologin_authentication_result_cb (PAMSession *session, int result, Display *display)
{
    g_signal_handlers_disconnect_matched (session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    if (display->priv->stopping)
        return;

    gboolean started_session = FALSE;

    if (result == PAM_SUCCESS)
    {
        g_debug ("User %s authorized", pam_session_get_username (display->priv->pam_session));
        pam_session_authorize (display->priv->pam_session);
        started_session = start_user_session (display);
        if (!started_session)
            g_debug ("Failed to start autologin session");
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
autologin (Display *display, const gchar *username, gboolean start_greeter_if_fail)
{
    gboolean result;
    GError *error = NULL;

    display->priv->start_greeter_if_fail = start_greeter_if_fail;

    display->priv->in_user_session = TRUE;
    display->priv->pam_session = pam_session_new (display->priv->pam_autologin_service, username);
    g_signal_connect (display->priv->pam_session, "got-messages", G_CALLBACK (autologin_pam_message_cb), display);
    g_signal_connect (display->priv->pam_session, "authentication-result", G_CALLBACK (autologin_authentication_result_cb), display);

    result = pam_session_start (display->priv->pam_session, &error);
    if (!result)
    {
        g_debug ("Failed to start autologin session for %s: %s", username, error->message);
        g_signal_handlers_disconnect_matched (display->priv->pam_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        pam_session_stop (display->priv->pam_session);
        g_object_unref (display->priv->pam_session);
        display->priv->pam_session = NULL;
    }
    g_clear_error (&error);

    return result;
}

static gboolean
autologin_guest (Display *display, gboolean start_greeter_if_fail)
{
    gchar *username;
    gboolean result;

    username = get_guest_username (display);
    if (!username)
        return FALSE;
    result = autologin (display, username, start_greeter_if_fail);
    g_free (username);

    return result;
}

static gboolean
cleanup_after_session (Display *display)
{
    /* Close ConsoleKit session */
    if (getuid () == 0)
        end_ck_session (session_get_cookie (display->priv->session));

    /* Close PAM session */
    g_signal_handlers_disconnect_matched (display->priv->pam_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    pam_session_stop (display->priv->pam_session);
    g_object_unref (display->priv->pam_session);
    display->priv->pam_session = NULL;

    g_signal_handlers_disconnect_matched (display->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    g_object_unref (display->priv->session);
    display->priv->session = NULL;

    if (display->priv->stopping)
    {
        check_stopped (display);
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
    if (greeter_get_guest_authenticated (display->priv->greeter))
    {
        started_session = autologin_guest (display, FALSE);
        if (!started_session)
            g_debug ("Failed to start guest session");
    }
    else
    {
        display->priv->in_user_session = TRUE;
        display->priv->pam_session = g_object_ref (greeter_get_pam_session (display->priv->greeter));
        started_session = start_user_session (display);
        if (!started_session)
            g_debug ("Failed to start user session");
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

    g_signal_emit (display, signals[SESSION_STOPPED], 0);

    if (cleanup_after_session (display))
        return;

    /* This display has ended */
    display_stop (display);
}

static Session *
create_session (Display *display, PAMSession *pam_session, const gchar *session_name, gboolean is_greeter, const gchar *log_filename)
{
    User *user;
    gchar *sessions_dir, *filename, *path, *command = NULL;
    GKeyFile *session_desktop_file;
    Session *session;
    gchar *cookie;
    gboolean result;
    GError *error = NULL;

    user = user_get_by_name (pam_session_get_username (pam_session));
    g_return_val_if_fail (user != NULL, FALSE);

    g_debug ("Starting session %s as user %s logging to %s", session_name, user_get_name (user), log_filename);

    // FIXME: This is X specific, move into xsession.c
    if (is_greeter)
        sessions_dir = display->priv->xgreeters_dir;
    else
        sessions_dir = display->priv->xsessions_dir;    

    filename = g_strdup_printf ("%s.desktop", session_name);
    path = g_build_filename (sessions_dir, filename, NULL);
    g_free (filename);

    session_desktop_file = g_key_file_new ();
    result = g_key_file_load_from_file (session_desktop_file, path, G_KEY_FILE_NONE, &error);
    if (!result)
        g_debug ("Failed to load session file %s: %s:", path, error->message);
    g_clear_error (&error);
    if (result)
    {
        command = g_key_file_get_string (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
        if (!command)
            g_warning ("No command in session file %s", path);
    }
    g_key_file_free (session_desktop_file);
    g_free (path);
    if (!command)
        return NULL;
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

    session = DISPLAY_GET_CLASS (display)->create_session (display);
    g_return_val_if_fail (session != NULL, NULL);
    g_signal_connect (session, "exited", G_CALLBACK (session_exited_cb), display);
    g_signal_connect (session, "terminated", G_CALLBACK (session_terminated_cb), display);
    if (is_greeter)
        g_signal_connect (session, "stopped", G_CALLBACK (greeter_session_stopped_cb), display);
    else
        g_signal_connect (session, "stopped", G_CALLBACK (user_session_stopped_cb), display);
    session_set_is_greeter (session, is_greeter);
    session_set_user (session, user);
    session_set_command (session, command);

    process_set_env (PROCESS (session), "DESKTOP_SESSION", session_name); // FIXME: Apparently deprecated?
    process_set_env (PROCESS (session), "GDMSESSION", session_name); // FIXME: Not cross-desktop

    set_env_from_pam_session (session, pam_session);

    process_set_log_file (PROCESS (session), log_filename);

    /* Open ConsoleKit session */
    if (getuid () == 0)
    {
        cookie = start_ck_session (display, is_greeter ? "LoginWindow" : "", user);
        session_set_cookie (session, cookie);
        g_free (cookie);
    }
    else
        session_set_cookie (session, g_getenv ("XDG_SESSION_COOKIE"));

    /* Connect using the session bus */
    if (getuid () != 0)
    {
        process_set_env (PROCESS (session), "DBUS_SESSION_BUS_ADDRESS", g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
        process_set_env (PROCESS (session), "LDM_BUS", "SESSION");
        process_set_env (PROCESS (session), "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
        process_set_env (PROCESS (session), "PATH", g_getenv ("PATH"));
    }

    /* Variables required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"))
    {
        process_set_env (PROCESS (session), "LIGHTDM_TEST_STATUS_SOCKET", g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"));
        process_set_env (PROCESS (session), "LIGHTDM_TEST_CONFIG", g_getenv ("LIGHTDM_TEST_CONFIG"));
        process_set_env (PROCESS (session), "LIGHTDM_TEST_HOME_DIR", g_getenv ("LIGHTDM_TEST_HOME_DIR"));
        process_set_env (PROCESS (session), "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    }
 
    return session;
}

static PAMSession *
greeter_start_authentication_cb (Greeter *greeter, const gchar *username, Display *display)
{
    return pam_session_new (display->priv->pam_service, username);
}

static gboolean
greeter_start_session_cb (Greeter *greeter, const gchar *session_name, Display *display)
{
    /* Store the session to use, use the default if none was requested */
    if (session_name)
    {
        g_free (display->priv->user_session);
        display->priv->user_session = g_strdup (session_name);
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
       if (switch_to_user (display, pam_session_get_username (greeter_get_pam_session (display->priv->greeter))))
           return TRUE;
    }

    /* Stop the greeter, the session will start when the greeter has quit */
    g_debug ("Stopping greeter");
    session_stop (display->priv->session);  

    return TRUE;
}

static gboolean
start_greeter_session (Display *display)
{
    User *user;
    gchar *log_dir, *filename, *log_filename;
    gboolean result;

    g_debug ("Starting greeter session");

    if (display->priv->greeter_user)
    {
        user = user_get_by_name (display->priv->greeter_user);
        if (!user)
        {
            g_debug ("Unable to start greeter, user %s does not exist", display->priv->greeter_user);
            return FALSE;
        }
    }
    else if (getuid () != 0)
        user = user_get_current ();
    else
    {
        g_warning ("Greeter must not be run as root");
        return FALSE;
    }
    display->priv->in_user_session = FALSE;
    display->priv->pam_session = pam_session_new (display->priv->pam_service, user_get_name (user));
    pam_session_authorize (display->priv->pam_session);
    g_object_unref (user);

    log_dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    // FIXME: May not be an X server
    filename = g_strdup_printf ("%s-greeter.log", xserver_get_address (XSERVER (display->priv->display_server)));
    log_filename = g_build_filename (log_dir, filename, NULL);
    g_free (log_dir);
    g_free (filename);

    display->priv->session = create_session (display, display->priv->pam_session, display->priv->greeter_session, TRUE, log_filename);
    g_free (log_filename);
    if (!display->priv->session)
        return FALSE;

    display->priv->greeter = greeter_new (display->priv->session);
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

    result = greeter_start (display->priv->greeter);
    if (result)
    {
        result = session_start (SESSION (display->priv->session));
        if (!result)
            g_debug ("Failed to start greeter session");
    }
    else
    {
        g_debug ("Failed to start greeter protocol");

        g_signal_handlers_disconnect_matched (display->priv->greeter, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        g_object_unref (display->priv->greeter);
        display->priv->greeter = NULL;
    }

    if (result)
    {
        display->priv->indicated_ready = TRUE;
        g_signal_emit (display, signals[READY], 0);
    }

    return result;
}

static gboolean
start_user_session (Display *display)
{
    GKeyFile *dmrc_file;
    User *user;
    gchar *log_filename;
    gboolean result = FALSE;

    g_debug ("Starting user session");

    user = user_get_by_name (pam_session_get_username (display->priv->pam_session));
    if (!user)
    {
        g_debug ("Unable to start session, user %s does not exist", pam_session_get_username (display->priv->pam_session));
        return FALSE;
    }

    /* Load the users login settings (~/.dmrc) */
    dmrc_file = dmrc_load (user_get_name (user));

    /* Update the .dmrc with changed settings */
    g_key_file_set_string (dmrc_file, "Desktop", "Session", display->priv->user_session);
    dmrc_save (dmrc_file, pam_session_get_username (display->priv->pam_session));
    g_key_file_free (dmrc_file);

    // FIXME: Copy old error file  
    log_filename = g_build_filename (user_get_home_directory (user), ".xsession-errors", NULL);
    g_object_unref (user);

    display->priv->session = create_session (display, display->priv->pam_session, display->priv->user_session, FALSE, log_filename);
    g_free (log_filename);

    if (display->priv->session)
        result = session_start (SESSION (display->priv->session));

    if (result)
    {
        if (!display->priv->indicated_ready)
            g_signal_emit (display, signals[READY], 0);
        g_signal_emit (display, signals[SESSION_STARTED], 0);
    }

    return result;
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
    gboolean started_session = FALSE;

    /* Don't run any sessions on local terminals */
    // FIXME: Make display_server_get_has_local_session
    if (IS_XSERVER_LOCAL (display_server) && xserver_local_get_xdmcp_server (XSERVER_LOCAL (display_server)))
        return;

    /* Automatically log in */
    if (display->priv->autologin_guest)
    {
        g_debug ("Automatically logging in as guest");
        started_session = autologin_guest (display, TRUE);
        if (!started_session)
            g_debug ("Failed to autologin as guest");
    }
    else if (display->priv->autologin_user)
    {
        g_debug ("Automatically logging in user %s", display->priv->autologin_user);
        started_session = autologin (display, display->priv->autologin_user, TRUE);
        if (!started_session)
            g_debug ("Failed to autologin user %s", display->priv->autologin_user);
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
    gboolean result;

    g_return_val_if_fail (display != NULL, FALSE);

    g_signal_connect (G_OBJECT (display->priv->display_server), "ready", G_CALLBACK (display_server_ready_cb), display);
    g_signal_connect (G_OBJECT (display->priv->display_server), "stopped", G_CALLBACK (display_server_stopped_cb), display);
    result = display_server_start (display->priv->display_server);

    g_signal_emit (display, signals[STARTED], 0);

    return result;
}

void
display_stop (Display *display)
{
    g_return_if_fail (display != NULL);

    if (!display->priv->stopping)
    {
        g_debug ("Stopping display");

        display->priv->stopping = TRUE;

        if (display->priv->display_server)
            display_server_stop (display->priv->display_server);
        if (display->priv->session)
            session_stop (display->priv->session);
    }

    check_stopped (display);
}

static gboolean
display_real_switch_to_user (Display *display, const gchar *username)
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

static Session *
display_create_session (Display *display)
{
    return NULL;
}

static void
display_finalize (GObject *object)
{
    Display *self;

    self = DISPLAY (object);

    if (self->priv->display_server)
        g_object_unref (self->priv->display_server);
    g_free (self->priv->greeter_user);
    g_free (self->priv->greeter_session);
    if (self->priv->greeter)
        g_object_unref (self->priv->greeter);
    g_free (self->priv->xsessions_dir);
    g_free (self->priv->xgreeters_dir);
    g_free (self->priv->session_wrapper);
    g_free (self->priv->pam_service);
    g_free (self->priv->pam_autologin_service);
    if (self->priv->session)
    {
        if (session_get_cookie (self->priv->session))
            end_ck_session (session_get_cookie (self->priv->session));
        g_object_unref (self->priv->session);
    }
    if (self->priv->pam_session)
        g_object_unref (self->priv->pam_session);
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
    klass->create_session = display_create_session;
    object_class->finalize = display_finalize;

    g_type_class_add_private (klass, sizeof (DisplayPrivate));

    signals[STARTED] =
        g_signal_new ("started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, started),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
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
                      ldm_marshal_BOOLEAN__STRING,
                      G_TYPE_BOOLEAN, 1, G_TYPE_STRING);
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
    signals[SESSION_STARTED] =
        g_signal_new ("session-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, session_started),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
    signals[SESSION_STOPPED] =
        g_signal_new ("session-stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, session_stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
