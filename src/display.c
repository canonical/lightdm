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
#include "theme.h"
#include "ldm-marshal.h"
#include "greeter.h"
#include "guest-account.h"
#include "xserver-local.h" // FIXME

/* Length of time in milliseconds to wait for a session to load */
#define USER_SESSION_TIMEOUT 5000

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

    /* Theme to use */
    gchar *greeter_theme;

    /* Program to run sessions through */
    gchar *session_wrapper;

    /* PAM service to authenticate against */
    gchar *pam_service;

    /* PAM service to authenticate against for automatic logins */
    gchar *pam_autologin_service;

    /* Greeter session process */
    Greeter *greeter_session;
    PAMSession *greeter_pam_session;
    gchar *greeter_ck_cookie;

    /* TRUE if the greeter can stay active during the session */
    gboolean supports_transitions;

    /* User session process */
    Session *user_session;
    gboolean using_guest_account;
    guint user_session_timer;
    PAMSession *user_pam_session;
    gchar *user_ck_cookie;

    /* User that should be automatically logged in */
    gchar *default_user;
    gboolean default_user_is_guest;
    gboolean default_user_requires_password;
    gint default_user_timeout;

    /* Default session */
    gchar *default_session;

    /* TRUE if stopping the display (waiting for dispaly server, greeter and session to stop) */
    gboolean stopping;    
};

G_DEFINE_TYPE (Display, display, G_TYPE_OBJECT);

static gboolean start_greeter (Display *display);

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
        display->priv->greeter_theme = config_get_string (config_get_instance (), config_section, "greeter-theme");
    if (!display->priv->greeter_theme)
        display->priv->greeter_theme = config_get_string (config_get_instance (), "SeatDefaults", "greeter-theme");
    if (config_section)
        display->priv->default_session = config_get_string (config_get_instance (), config_section, "xsession");
    if (!display->priv->default_session)
        display->priv->default_session = config_get_string (config_get_instance (), "SeatDefaults", "xsession");
    if (config_section)
        display->priv->session_wrapper = config_get_string (config_get_instance (), config_section, "xsession-wrapper");
    if (!display->priv->session_wrapper)
        display->priv->session_wrapper = config_get_string (config_get_instance (), "SeatDefaults", "xsession-wrapper");
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

Greeter *
display_get_greeter (Display *display)
{
    g_return_val_if_fail (display != NULL, NULL);
    return display->priv->greeter_session;
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

const gchar *
display_get_session_user (Display *display)
{
    g_return_val_if_fail (display != NULL, NULL);

    if (display->priv->user_session)
        return pam_session_get_username (display->priv->user_pam_session);
    else
        return NULL;
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
run_script (const gchar *script)
{
    // FIXME
}

static void
user_session_exited_cb (Session *session, gint status, Display *display)
{
    if (status != 0)
        g_debug ("User session exited with value %d", status);
}

static void
user_session_terminated_cb (Session *session, gint signum, Display *display)
{
    g_debug ("User session terminated with signal %d", signum);
}

static void
check_stopped (Display *display)
{
    if (display->priv->stopping &&
        display->priv->display_server == NULL &&
        display->priv->greeter_session == NULL &&
        display->priv->user_session == NULL)
    {
        g_debug ("Display stopped");
        g_signal_emit (display, signals[STOPPED], 0);
    }
}

static void
user_session_stopped_cb (Session *session, Display *display)
{
    g_signal_handlers_disconnect_matched (display->priv->user_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);

    /* If a guest account, remove the account on exit */
    if (display->priv->using_guest_account)
        guest_account_unref ();

    g_object_unref (display->priv->user_session);
    display->priv->user_session = NULL;
    check_stopped (display);

    if (!display->priv->stopping)
    {
        run_script ("PostSession");

        if (display->priv->user_session_timer)
        {
            g_source_remove (display->priv->user_session_timer);
            display->priv->user_session_timer = 0;
        }

        pam_session_end (display->priv->user_pam_session);
        g_object_unref (display->priv->user_pam_session);
        display->priv->user_pam_session = NULL;

        end_ck_session (display->priv->user_ck_cookie);
        g_free (display->priv->user_ck_cookie);
        display->priv->user_ck_cookie = NULL;

        /* Restart the X server or start a new one if it failed */
        if (!display_server_restart (display->priv->display_server))
        {
            g_debug ("Starting new X server");
            display_server_start (display->priv->display_server);
        }
    }
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

static gboolean
really_start_user_session (Display *display)
{
    gboolean result;

    g_debug ("Starting user session");

    /* Open ConsoleKit session */
    display->priv->user_ck_cookie = start_ck_session (display, "", session_get_user (display->priv->user_session));
    if (display->priv->user_ck_cookie)
        child_process_set_env (CHILD_PROCESS (display->priv->user_session), "XDG_SESSION_COOKIE", display->priv->user_ck_cookie);

    session_set_has_pipe (SESSION (display->priv->greeter_session), FALSE);
    result = session_start (display->priv->user_session);

    /* Create guest account */
    if (result && display->priv->using_guest_account)
        guest_account_ref ();

    return result;
}

static Session *
display_start_session (Display *display)
{
    return NULL;
}

static Session *
start_session (Display *display)
{
    return DISPLAY_GET_CLASS (display)->start_session (display);
}

static gboolean
start_user_session (Display *display, const gchar *session)
{
    gchar *filename, *path, *xsessions_dir;
    gchar *command, *log_filename;
    User *user;
    gboolean supports_transitions;
    GKeyFile *dmrc_file, *session_desktop_file;
    gboolean result;
    GError *error = NULL;

    run_script ("PreSession");

    g_debug ("Preparing '%s' session for user %s", session, pam_session_get_username (display->priv->user_pam_session));

    /* Once logged in, don't autologin again */
    g_free (display->priv->default_user);
    display->priv->default_user = NULL;
    display->priv->default_user_is_guest = FALSE;

    /* Load the users login settings (~/.dmrc) */
    dmrc_file = dmrc_load (pam_session_get_username (display->priv->user_pam_session));

    /* Update the .dmrc with changed settings */
    g_key_file_set_string (dmrc_file, "Desktop", "Session", session);

    xsessions_dir = config_get_string (config_get_instance (), "Directories", "xsessions-directory");
    filename = g_strdup_printf ("%s.desktop", session);
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
    supports_transitions = g_key_file_get_boolean (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-Supports-Transitions", NULL);
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

    user = user_get_by_name (pam_session_get_username (display->priv->user_pam_session));
    if (!user)
    {
        g_free (command);
        g_warning ("Unable to start session, user %s does not exist", pam_session_get_username (display->priv->user_pam_session));
        return FALSE;
    }

    display->priv->supports_transitions = supports_transitions;
    display->priv->user_session = start_session (display);
    g_signal_connect (G_OBJECT (display->priv->user_session), "exited", G_CALLBACK (user_session_exited_cb), display);
    g_signal_connect (G_OBJECT (display->priv->user_session), "terminated", G_CALLBACK (user_session_terminated_cb), display);
    g_signal_connect (G_OBJECT (display->priv->user_session), "stopped", G_CALLBACK (user_session_stopped_cb), display);

    session_set_user (display->priv->user_session, user);
    session_set_command (display->priv->user_session, command);
    child_process_set_env (CHILD_PROCESS (display->priv->user_session), "PATH", "/usr/local/bin:/usr/bin:/bin");
    child_process_set_env (CHILD_PROCESS (display->priv->user_session), "USER", user_get_name (user));
    child_process_set_env (CHILD_PROCESS (display->priv->user_session), "USERNAME", user_get_name (user)); // FIXME: Is this required?
    child_process_set_env (CHILD_PROCESS (display->priv->user_session), "HOME", user_get_home_directory (user));
    child_process_set_env (CHILD_PROCESS (display->priv->user_session), "SHELL", user_get_shell (user));
    child_process_set_env (CHILD_PROCESS (display->priv->user_session), "DESKTOP_SESSION", session); // FIXME: Apparently deprecated?
    child_process_set_env (CHILD_PROCESS (display->priv->user_session), "GDMSESSION", session); // FIXME: Not cross-desktop
    set_env_from_pam_session (display->priv->user_session, display->priv->user_pam_session);

    // FIXME: Copy old error file  
    log_filename = g_build_filename (user_get_home_directory (user), ".xsession-errors", NULL);
    g_debug ("Logging to %s", log_filename);
    child_process_set_log_file (CHILD_PROCESS (display->priv->user_session), log_filename);
    g_free (log_filename);

    /* Connect using the session bus */
    if (getuid () != 0)
    {
        child_process_set_env (CHILD_PROCESS (display->priv->user_session), "DBUS_SESSION_BUS_ADDRESS", getenv ("DBUS_SESSION_BUS_ADDRESS"));
        child_process_set_env (CHILD_PROCESS (display->priv->user_session), "XDG_SESSION_COOKIE", getenv ("XDG_SESSION_COOKIE"));
        child_process_set_env (CHILD_PROCESS (display->priv->user_session), "LDM_BUS", "SESSION");
    }

    /* Variable required for regression tests */
    if (getenv ("LIGHTDM_TEST_STATUS_SOCKET"))
    {
        child_process_set_env (CHILD_PROCESS (display->priv->user_session), "LIGHTDM_TEST_STATUS_SOCKET", getenv ("LIGHTDM_TEST_STATUS_SOCKET"));
        child_process_set_env (CHILD_PROCESS (display->priv->user_session), "LIGHTDM_TEST_CONFIG", getenv ("LIGHTDM_TEST_CONFIG"));
        child_process_set_env (CHILD_PROCESS (display->priv->user_session), "LIGHTDM_TEST_HOME_DIR", getenv ("LIGHTDM_TEST_HOME_DIR"));
        child_process_set_env (CHILD_PROCESS (display->priv->user_session), "LD_LIBRARY_PATH", getenv ("LD_LIBRARY_PATH"));
    }

    g_object_unref (user);
    g_free (command);

    /* Start it now, or wait for the greeter to quit */
    if (display->priv->greeter_session == NULL || display->priv->supports_transitions)
        result = really_start_user_session (display);
    else
    {
        g_debug ("Waiting for greeter to quit before starting user session process");
        result = TRUE;
    }

    /* Save modified DMRC */
    dmrc_save (dmrc_file, pam_session_get_username (display->priv->user_pam_session));
    g_key_file_free (dmrc_file);

    return result;
}

static gboolean
activate_user (Display *display, const gchar *username)
{
    gboolean result;

    g_signal_emit (display, signals[ACTIVATE_USER], 0, username, &result);

    return result;
}

static void
start_greeter_for_autologin (Display *display)
{
    /* Cancel attempt at autologin */
    pam_session_cancel (display->priv->user_pam_session);
    g_object_unref (display->priv->user_pam_session);
    display->priv->user_pam_session = NULL;

    /* Start greeter and select user that failed */
    start_greeter (display);
}

static void
autologin_pam_message_cb (PAMSession *session, int num_msg, const struct pam_message **msg, Display *display)
{
    g_debug ("Aborting automatic login and starting greeter, PAM requests input");
    start_greeter_for_autologin (display);
}

static void
autologin_authentication_result_cb (PAMSession *session, int result, Display *display)
{
    if (result == PAM_SUCCESS)
    {
        g_debug ("User %s authorized", pam_session_get_username (session));

        if (activate_user (display, pam_session_get_username (display->priv->user_pam_session)))
            return;

        pam_session_authorize (session);
        start_user_session (display, display->priv->default_session);
    }
    else
        start_greeter_for_autologin (display);
}
  
static gboolean
session_timeout_cb (Display *display)
{
    g_warning ("Session has not indicated it is ready, stopping greeter anyway");

    /* Stop the greeter */
    greeter_quit (display->priv->greeter_session);

    display->priv->user_session_timer = 0;
    return FALSE;
}

static void
greeter_start_session_cb (Greeter *greeter, const gchar *session, gboolean is_guest, Display *display)
{
    /* Default session requested */
    if (strcmp (session, "") == 0)
        session = display->priv->default_session;

    display->priv->user_pam_session = greeter_get_pam_session (greeter);

    if (!display->priv->user_pam_session ||
        !pam_session_get_in_session (display->priv->user_pam_session))
    {
        g_warning ("Ignoring request for login with unauthenticated user");
        return;
    }

    /* Stop this display if can switch to that user */
    if (activate_user (display, pam_session_get_username (display->priv->user_pam_session)))
    {
        display_stop (display);
        return;
    }

    start_user_session (display, session);

    /* Stop session, waiting for user session to indicate it is ready (if supported) */
    // FIXME: Hard-coded timeout
    // FIXME: Greeter quit timeout
    if (display->priv->supports_transitions)
        display->priv->user_session_timer = g_timeout_add (USER_SESSION_TIMEOUT, (GSourceFunc) session_timeout_cb, display);
    else
        greeter_quit (display->priv->greeter_session);
}

static void
greeter_stopped_cb (Greeter *greeter, Display *display)
{
    g_debug ("Greeter quit");

    pam_session_end (display->priv->greeter_pam_session);
    g_object_unref (display->priv->greeter_pam_session);
    display->priv->greeter_pam_session = NULL;

    end_ck_session (display->priv->greeter_ck_cookie);
    g_free (display->priv->greeter_ck_cookie);
    display->priv->greeter_ck_cookie = NULL;   

    g_signal_handlers_disconnect_matched (display->priv->greeter_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
    g_object_unref (display->priv->greeter_session);
    display->priv->greeter_session = NULL;
  
    if (display->priv->stopping)
        check_stopped (display);
    else
    {
        /* Start session if waiting for greeter to quit */
        if (display->priv->user_session && child_process_get_pid (CHILD_PROCESS (display->priv->user_session)) == 0)
            really_start_user_session (display);
    }
}

static gboolean
start_greeter (Display *display)
{
    GKeyFile *theme;
    gchar *command, *log_dir, *filename, *log_filename;
    User *user;
    gboolean result;
    GError *error = NULL;

    theme = load_theme (display->priv->greeter_theme, &error);
    if (!theme)
        g_warning ("Failed to find theme %s: %s", display->priv->greeter_theme, error->message);
    g_clear_error (&error);
    if (!theme)
        return FALSE;

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

    g_debug ("Starting greeter %s as user %s", display->priv->greeter_theme, user_get_name (user));

    display->priv->greeter_pam_session = pam_session_new (display->priv->pam_service, user_get_name (user));
    pam_session_authorize (display->priv->greeter_pam_session);

    display->priv->greeter_ck_cookie = start_ck_session (display, "LoginWindow", user);

    // FIXME: Need to create based on the display type
    display->priv->greeter_session = greeter_new (display->priv->greeter_theme);
    if (display->priv->default_user)
        greeter_set_selected_user (display->priv->greeter_session, display->priv->default_user, display->priv->default_user_timeout);
    else if (display->priv->default_user_is_guest)
        greeter_set_selected_user (display->priv->greeter_session, guest_account_get_username (), 0);
    greeter_set_default_session (display->priv->greeter_session, display->priv->default_session);
    g_signal_connect (G_OBJECT (display->priv->greeter_session), "start-session", G_CALLBACK (greeter_start_session_cb), display);
    g_signal_connect (G_OBJECT (display->priv->greeter_session), "stopped", G_CALLBACK (greeter_stopped_cb), display);
    session_set_user (SESSION (display->priv->greeter_session), user);
    command = theme_get_command (theme);
    session_set_command (SESSION (display->priv->greeter_session), command);
    g_free (command);

    child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "PATH", "/usr/local/bin:/usr/bin:/bin");
    child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "USER", user_get_name (user));
    child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "HOME", user_get_home_directory (user));
    child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "SHELL", user_get_shell (user));
    if (display->priv->greeter_ck_cookie)
        child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "XDG_SESSION_COOKIE", display->priv->greeter_ck_cookie);
    set_env_from_pam_session (SESSION (display->priv->greeter_session), display->priv->greeter_pam_session);

    log_dir = config_get_string (config_get_instance (), "Directories", "log-directory");
    // FIXME: May not be an X server
    filename = g_strdup_printf ("%s-greeter.log", xserver_get_address (XSERVER (display->priv->display_server)));
    log_filename = g_build_filename (log_dir, filename, NULL);
    g_free (log_dir);
    g_free (filename);
    g_debug ("Logging to %s", log_filename);
    child_process_set_log_file (CHILD_PROCESS (display->priv->greeter_session), log_filename);
    g_free (log_filename);
  
    /* Connect using the session bus */
    if (getuid () != 0)
    {
        child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "DBUS_SESSION_BUS_ADDRESS", getenv ("DBUS_SESSION_BUS_ADDRESS"));
        child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "XDG_SESSION_COOKIE", getenv ("XDG_SESSION_COOKIE"));
        child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "LDM_BUS", "SESSION");
    }

    /* Variable required for regression tests */
    if (getenv ("LIGHTDM_TEST_STATUS_SOCKET"))
    {
        child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "LIGHTDM_TEST_STATUS_SOCKET", getenv ("LIGHTDM_TEST_STATUS_SOCKET"));
        child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "LIGHTDM_TEST_CONFIG", getenv ("LIGHTDM_TEST_CONFIG"));
        child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "LIGHTDM_TEST_HOME_DIR", getenv ("LIGHTDM_TEST_HOME_DIR"));
        child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "LD_LIBRARY_PATH", getenv ("LD_LIBRARY_PATH"));
    }

    session_set_has_pipe (SESSION (display->priv->greeter_session), TRUE);
    result = session_start (SESSION (display->priv->greeter_session));

    g_key_file_free (theme);
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
        /* Stop the user session then start a new X server */
        if (display->priv->user_session)
        {
            g_debug ("Stopping session");
            child_process_stop (CHILD_PROCESS (display->priv->user_session));
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
    const gchar *autologin_user = NULL;

    run_script ("Init"); // FIXME: Async

    /* Don't run any sessions on local terminals */
    if (IS_XSERVER_LOCAL (display_server) && xserver_local_get_xdmcp_server (XSERVER_LOCAL (display_server)))
        return;

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
 
        /* Run using autologin PAM session, abort if get asked any questions */      
        g_debug ("Automatically logging in user %s", autologin_user);

        if (display->priv->default_user_requires_password)
            pam_service = display->priv->pam_service;
        else
            pam_service = display->priv->pam_autologin_service;

        display->priv->user_pam_session = pam_session_new (pam_service, autologin_user);
        g_signal_connect (display->priv->user_pam_session, "got-messages", G_CALLBACK (autologin_pam_message_cb), display);
        g_signal_connect (display->priv->user_pam_session, "authentication-result", G_CALLBACK (autologin_authentication_result_cb), display);
        if (pam_session_start (display->priv->user_pam_session, &error))
            return;
        else
            g_warning ("Failed to autologin user %s, starting greeter instead: %s", autologin_user, error->message);
        g_clear_error (&error);
    }

    start_greeter (display);
}

const gchar *
display_get_session_cookie (Display *display)
{
    g_return_val_if_fail (display != NULL, NULL);
    if (display->priv->user_ck_cookie)
        return display->priv->user_ck_cookie;
    else
        return display->priv->greeter_ck_cookie;
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
    if (display->priv->greeter_session)
        child_process_stop (CHILD_PROCESS (display->priv->greeter_session));
    if (display->priv->user_session)
        child_process_stop (CHILD_PROCESS (display->priv->user_session));
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
    g_free (self->priv->greeter_theme);
    g_free (self->priv->session_wrapper);
    g_free (self->priv->pam_service);
    g_free (self->priv->pam_autologin_service);
    if (self->priv->greeter_session)
        g_object_unref (self->priv->greeter_session);
    if (self->priv->greeter_pam_session)
        g_object_unref (self->priv->greeter_pam_session);
    end_ck_session (self->priv->greeter_ck_cookie);
    g_free (self->priv->greeter_ck_cookie);
    if (self->priv->user_session)
        g_object_unref (self->priv->user_session);
    if (self->priv->user_session_timer)
        g_source_remove (self->priv->user_session_timer);
    if (self->priv->user_pam_session)
        g_object_unref (self->priv->user_pam_session);
    end_ck_session (self->priv->user_ck_cookie);
    g_free (self->priv->user_ck_cookie);
    g_free (self->priv->default_user);
    g_free (self->priv->default_session);

    G_OBJECT_CLASS (display_parent_class)->finalize (object);
}

static void
display_class_init (DisplayClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->start_session = display_start_session;
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
