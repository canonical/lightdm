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
#include "guest-account.h"
#include "xserver-local.h" // FIXME

enum {
    STARTED,
    ACTIVATE_USER,
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

    /* Default session for users */
    gchar *default_session;

    /* Session requested to log into */
    gchar *user_session;

    /* Program to run sessions through */
    gchar *session_wrapper;

    /* PAM service to authenticate against */
    gchar *pam_service;

    /* PAM service to authenticate against for automatic logins */
    gchar *pam_autologin_service;
  
    /* Session process */
    Session *session;

    /* Communication link to greeter */
    Greeter *greeter;

    // FIXME: Handle in Session?
    gboolean using_guest_account;

    PAMSession *pam_session;

    /* User that should be automatically logged in */
    gchar *default_user;
    gboolean default_user_is_guest;
    gboolean default_user_requires_password;
    gint default_user_timeout;

    /* TRUE if stopping the display (waiting for dispaly server, greeter and session to stop) */
    gboolean stopping;    
};

G_DEFINE_TYPE (Display, display, G_TYPE_OBJECT);

static gboolean start_greeter_session (Display *display);
static gboolean start_user_session (Display *display, PAMSession *pam_session, const gchar *name);

// FIXME: Should be a construct property
void
display_load_config (Display *display, const gchar *config_section)
{
    g_return_if_fail (display != NULL);
    
    if (config_section)
        display->priv->greeter_user = config_get_string (config_get_instance (), config_section, "greeter-user");
    if (!display->priv->greeter_user)
        display->priv->greeter_user = config_get_string (config_get_instance (), "SeatDefaults", "greeter-user");
    if (config_section)
        display->priv->greeter_session = config_get_string (config_get_instance (), config_section, "greeter-session");
    if (!display->priv->greeter_session)
        display->priv->greeter_session = config_get_string (config_get_instance (), "SeatDefaults", "greeter-session");
    if (config_section)
        display->priv->default_session = config_get_string (config_get_instance (), config_section, "default-session");
    if (!display->priv->default_session)
        display->priv->default_session = config_get_string (config_get_instance (), "SeatDefaults", "default-session");
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

Session *
display_get_session (Display *display)
{
    g_return_val_if_fail (display != NULL, NULL);
    return display->priv->session;
}

void
display_set_default_user (Display *display, const gchar *username, gboolean is_guest, gboolean requires_password, gint timeout)
{
    g_return_if_fail (display != NULL);
    g_free (display->priv->default_user);
    display->priv->default_user = g_strdup (username);
    display->priv->default_user_is_guest = is_guest;
    display->priv->default_user_requires_password = requires_password;
    display->priv->default_user_timeout = timeout;
}

static gboolean
activate_user (Display *display, const gchar *username)
{
    gboolean result;
    g_signal_emit (display, signals[ACTIVATE_USER], 0, username, &result);
    return result;
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
                child_process_set_env (CHILD_PROCESS (session), pam_env_vars[0], pam_env_vars[1]);
            else
                g_warning ("Can't parse PAM environment variable %s", pam_env[i]);
            g_strfreev (pam_env_vars);
        }
        g_strfreev (pam_env);
    }
}

static Session *
display_create_session (Display *display)
{
    return NULL;
}

static Session *
create_session (Display *display)
{
    return DISPLAY_GET_CLASS (display)->create_session (display);
}

static void
session_exited_cb (Session *session, gint status, Display *display)
{
    if (status != 0)
        g_debug ("User session exited with value %d", status);
}

static void
session_terminated_cb (Session *session, gint signum, Display *display)
{
    g_debug ("User session terminated with signal %d", signum);
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
session_stopped_cb (Session *session, Display *display)
{
    PAMSession *pam_session = NULL;

    if (display->priv->greeter)
        g_debug ("Greeter quit");
    else
        g_debug ("Session quit");

    /* If a guest account, remove the account on exit */
    // FIXME: Move into Session
    if (display->priv->using_guest_account) // FIXME: Not set
    {
        display->priv->using_guest_account = FALSE;
        guest_account_unref ();
    }

    if (display->priv->stopping)
    {
        check_stopped (display);
        return;
    }

    g_signal_handlers_disconnect_matched (display->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);

    end_ck_session (session_get_cookie (display->priv->session));

    pam_session_end (display->priv->pam_session);
    g_object_unref (display->priv->pam_session);
    display->priv->pam_session = NULL;

    g_object_unref (display->priv->session);
    display->priv->session = NULL;

    /* Restart the X server or start a new one if it failed */
    if (!display_server_restart (display->priv->display_server))
    {
        g_debug ("Starting new display server");
        display_server_start (display->priv->display_server);
    }

    if (display->priv->greeter)
        pam_session = greeter_get_pam_session (display->priv->greeter);
    if (pam_session && pam_session_get_in_session (pam_session) && display->priv->user_session)
    {
        start_user_session (display, pam_session, display->priv->user_session);
        g_free (display->priv->user_session);
        display->priv->user_session = NULL;
    }
    else
        start_greeter_session (display);

    if (display->priv->greeter)
    {
        g_signal_handlers_disconnect_matched (display->priv->greeter, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        g_object_unref (display->priv->greeter);
        display->priv->greeter = NULL;
    }
}

static gboolean
start_session (Display *display, PAMSession *pam_session, const gchar *session_name, gboolean is_greeter, const gchar *log_filename)
{
    User *user;
    gchar *xsessions_dir, *filename, *path, *command;
    GKeyFile *session_desktop_file;
    Session *session;
    gchar *cookie;
    gboolean result;
    GError *error = NULL;

    /* Can't be any session running already */
    g_return_val_if_fail (display->priv->session == NULL, FALSE);
    g_return_val_if_fail (display->priv->pam_session == NULL, FALSE);

    user = user_get_by_name (pam_session_get_username (pam_session));
    g_return_val_if_fail (user != NULL, FALSE);

    g_debug ("Starting session %s as user %s logging to %s", session_name, user_get_name (user), log_filename);

    xsessions_dir = config_get_string (config_get_instance (), "Directories", "xsessions-directory");
    filename = g_strdup_printf ("%s.desktop", session_name);
    path = g_build_filename (xsessions_dir, filename, NULL);
    g_free (xsessions_dir);
    g_free (filename);

    session_desktop_file = g_key_file_new ();
    result = g_key_file_load_from_file (session_desktop_file, path, G_KEY_FILE_NONE, &error);
    g_free (path);
    if (!result)
        g_warning ("Failed to load session file %s: %s:", path, error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;
    command = g_key_file_get_string (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
    g_key_file_free (session_desktop_file);

    if (!command)
    {
        g_warning ("No command in session file %s", path);
        return FALSE;
    }
    if (display->priv->session_wrapper)
    {
        gchar *t = command;
        command = g_strdup_printf ("%s '%s'", display->priv->session_wrapper, command);
        g_free (t);
    }

    session = create_session (display);
    g_signal_connect (session, "exited", G_CALLBACK (session_exited_cb), display);
    g_signal_connect (session, "terminated", G_CALLBACK (session_terminated_cb), display);
    g_signal_connect (session, "stopped", G_CALLBACK (session_stopped_cb), display);
    session_set_is_greeter (session, is_greeter);
    session_set_user (session, user);
    session_set_command (session, command);

    child_process_set_env (CHILD_PROCESS (session), "DESKTOP_SESSION", session_name); // FIXME: Apparently deprecated?
    child_process_set_env (CHILD_PROCESS (session), "GDMSESSION", session_name); // FIXME: Not cross-desktop
    set_env_from_pam_session (session, pam_session);

    child_process_set_log_file (CHILD_PROCESS (session), log_filename);

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
        child_process_set_env (CHILD_PROCESS (session), "DBUS_SESSION_BUS_ADDRESS", g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
        child_process_set_env (CHILD_PROCESS (session), "LDM_BUS", "SESSION");
    }

    /* Variables required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"))
    {
        child_process_set_env (CHILD_PROCESS (session), "LIGHTDM_TEST_STATUS_SOCKET", g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"));
        child_process_set_env (CHILD_PROCESS (session), "LIGHTDM_TEST_CONFIG", g_getenv ("LIGHTDM_TEST_CONFIG"));
        child_process_set_env (CHILD_PROCESS (session), "LIGHTDM_TEST_HOME_DIR", g_getenv ("LIGHTDM_TEST_HOME_DIR"));
        child_process_set_env (CHILD_PROCESS (session), "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    }

    pam_session_authorize (pam_session);

    result = session_start (SESSION (session));
  
    if (result)
    {
        display->priv->session = g_object_ref (session);
        display->priv->pam_session = g_object_ref (pam_session);
    }
    else
       g_signal_handlers_disconnect_matched (display->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);

    return result;
}

static void
autologin_pam_message_cb (PAMSession *session, int num_msg, const struct pam_message **msg, Display *display)
{
    g_debug ("Aborting automatic login and starting greeter, PAM requests input");
    pam_session_cancel (session);
}

static void
autologin_authentication_result_cb (PAMSession *session, int result, Display *display)
{
    if (result == PAM_SUCCESS)
    {
        g_debug ("User %s authorized", pam_session_get_username (session));

        if (activate_user (display, pam_session_get_username (session)))
            return;

        pam_session_authorize (session);
        start_user_session (display, session, display->priv->default_session);
    }
    else
    {
        /* Start greeter and select user that failed */
        start_greeter_session (display);
    }

    g_object_unref (session);
}

static void
greeter_start_session_cb (Greeter *greeter, const gchar *session, gboolean is_guest, Display *display)
{
    PAMSession *pam_session;

    /* Default session requested */
    if (!session)
        session = display->priv->default_session;
    g_free (display->priv->user_session);
    display->priv->user_session = g_strdup (session);

    pam_session = greeter_get_pam_session (display->priv->greeter);

    if (!pam_session || !pam_session_get_in_session (pam_session))
    {
        g_warning ("Ignoring request for login with unauthenticated user");
        return;
    }

    /* Stop this display if can switch to that user */
    if (activate_user (display, pam_session_get_username (pam_session)))
    {
        display_stop (display);
        return;
    }

    greeter_quit (display->priv->greeter);
}

static gboolean
start_greeter_session (Display *display)
{
    User *user;
    const gchar *autologin_user = NULL;
    gchar *log_dir, *filename, *log_filename;
    PAMSession *pam_session;
    gboolean result;

    g_debug ("Starting greeter session");

    /* If have user then automatically login the first time */
    if (display->priv->default_user_is_guest)
    {
        autologin_user = guest_account_get_username ();
        guest_account_ref ();
    }
    else if (display->priv->default_user)
        autologin_user = display->priv->default_user;

    if (autologin_user)
    {
        GError *error = NULL;
        gchar *pam_service;
        PAMSession *autologin_pam_session;
 
        /* Run using autologin PAM session, abort if get asked any questions */      
        g_debug ("Automatically logging in user %s", autologin_user);

        if (display->priv->default_user_requires_password)
            pam_service = display->priv->pam_service;
        else
            pam_service = display->priv->pam_autologin_service;

        autologin_pam_session = pam_session_new (pam_service, autologin_user);
        g_signal_connect (autologin_pam_session, "got-messages", G_CALLBACK (autologin_pam_message_cb), display);
        g_signal_connect (autologin_pam_session, "authentication-result", G_CALLBACK (autologin_authentication_result_cb), display);
        if (pam_session_start (autologin_pam_session, &error))
            return TRUE;
        else
        {
            g_object_unref (autologin_pam_session);
            g_warning ("Failed to autologin user %s, starting greeter instead: %s", autologin_user, error->message);
        }
        g_clear_error (&error);
    }

    if (display->priv->greeter_user)
    {
        user = user_get_by_name (display->priv->greeter_user);
        if (!user)
        {
            g_warning ("Unable to start greeter, user %s does not exist", display->priv->greeter_user);
            return FALSE;
        }
    }
    else
        user = user_get_current ();

    pam_session = pam_session_new (display->priv->pam_service, user_get_name (user));
    pam_session_authorize (pam_session);

    log_dir = config_get_string (config_get_instance (), "Directories", "log-directory");
    // FIXME: May not be an X server
    filename = g_strdup_printf ("%s-greeter.log", xserver_get_address (XSERVER (display->priv->display_server)));
    log_filename = g_build_filename (log_dir, filename, NULL);
    g_free (log_dir);
    g_free (filename);

    result = start_session (display, pam_session, display->priv->greeter_session, TRUE, log_filename);
    g_object_unref (pam_session);

    if (result)
    {
        display->priv->greeter = greeter_new (display->priv->session);
        g_signal_connect (G_OBJECT (display->priv->greeter), "start-session", G_CALLBACK (greeter_start_session_cb), display);
        if (display->priv->default_user)
            greeter_set_selected_user (display->priv->greeter, display->priv->default_user, display->priv->default_user_timeout);
        else if (display->priv->default_user_is_guest)
            greeter_set_selected_user (display->priv->greeter, guest_account_get_username (), 0);
        greeter_set_default_session (display->priv->greeter, display->priv->default_session);
    }
    else
    {
        g_signal_handlers_disconnect_matched (display->priv->greeter, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        g_object_unref (display->priv->greeter);
        display->priv->greeter = NULL;
    }

    g_free (log_filename);
    g_object_unref (user);
  
    return result;
}

static gboolean
start_user_session (Display *display, PAMSession *pam_session, const gchar *name)
{
    GKeyFile *dmrc_file;
    User *user;
    gchar *log_filename;
    gboolean result;

    g_debug ("Starting user session");

    user = user_get_by_name (pam_session_get_username (pam_session));
    if (!user)
    {
        g_warning ("Unable to start session, user %s does not exist", pam_session_get_username (pam_session));
        return FALSE;
    }

    /* Load the users login settings (~/.dmrc) */
    dmrc_file = dmrc_load (user_get_name (user));

    /* Update the .dmrc with changed settings */
    g_key_file_set_string (dmrc_file, "Desktop", "Session", name);

    /* Save modified DMRC */
    dmrc_save (dmrc_file, pam_session_get_username (pam_session));
    g_key_file_free (dmrc_file);

    /* Once logged in, don't autologin again */
    g_free (display->priv->default_user);
    display->priv->default_user = NULL;
    display->priv->default_user_is_guest = FALSE;

    // FIXME: Copy old error file  
    log_filename = g_build_filename (user_get_home_directory (user), ".xsession-errors", NULL);

    result = start_session (display, pam_session, name, FALSE, log_filename);

    g_object_unref (pam_session);
    g_free (log_filename);
    g_object_unref (user);

    return result;
}

static void
display_server_stopped_cb (DisplayServer *server, Display *display)
{
    g_debug ("X server stopped");

    if (display->priv->stopping)
    {
        g_object_unref (display->priv->display_server);
        display->priv->display_server = NULL;
        check_stopped (display);
    }
    else
    {
        /* Stop the session then start a new X server */
        if (display->priv->session)
        {
            g_debug ("Stopping session");
            child_process_stop (CHILD_PROCESS (display->priv->session));
        }
        else
        {
            g_debug ("Starting new X server");
            display_server_start (display->priv->display_server);
        }
    }
}

static void
display_server_ready_cb (DisplayServer *display_server, Display *display)
{
    /* Don't run any sessions on local terminals */
    // FIXME: Make display_server_get_has_local_session
    if (IS_XSERVER_LOCAL (display_server) && xserver_local_get_xdmcp_server (XSERVER_LOCAL (display_server)))
        return;

    start_greeter_session (display);
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

    g_debug ("Stopping display");

    display->priv->stopping = TRUE;

    display_server_stop (display->priv->display_server);
    if (display->priv->session)
        child_process_stop (CHILD_PROCESS (display->priv->session));
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

    g_object_unref (self->priv->display_server);
    g_free (self->priv->greeter_user);
    g_free (self->priv->greeter_session);
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
    g_free (self->priv->default_user);
    g_free (self->priv->default_session);
    g_free (self->priv->user_session);

    G_OBJECT_CLASS (display_parent_class)->finalize (object);
}

static void
display_class_init (DisplayClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

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
    signals[ACTIVATE_USER] =
        g_signal_new ("activate-user",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, activate_user),
                      g_signal_accumulator_true_handled,
                      NULL,
                      ldm_marshal_BOOLEAN__STRING,
                      G_TYPE_BOOLEAN, 1, G_TYPE_STRING);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
