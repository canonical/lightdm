/*
 * Copyright (C) 2010 Robert Ancell.
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
#include <errno.h>
#include <pwd.h>
#include <gio/gdesktopappinfo.h>

#include "display.h"
#include "pam-session.h"
#include "theme.h"
#include "ldm-marshal.h"
#include "greeter-protocol.h"

/* Length of time in milliseconds to wait for a session to load */
#define USER_SESSION_TIMEOUT 5000

/* Length of time in milliseconds to wait for a greeter to quit */
#define GREETER_QUIT_TIMEOUT 1000

enum {
    START_GREETER,
    END_GREETER,
    START_SESSION,
    END_SESSION,
    EXITED,
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
    gint index;

    /* X server */
    XServer *xserver;
  
    /* Number of times have logged in */
    gint login_count;

    /* Layout to use in greeter/sessions */
    gchar *default_layout;

    /* User to run greeter as */
    gchar *greeter_user;

    /* Theme to use */
    gchar *greeter_theme;

    /* Program to run sessions through */
    gchar *session_wrapper;

    /* PAM service to authenticate against */
    gchar *pam_service;

    /* Pipe to communicate to greeter */
    int greeter_pipe[2];

    /* Greeter session process */
    Session *greeter_session;
    gboolean greeter_connected;
    guint greeter_quit_timeout;
    PAMSession *greeter_pam_session;
    gchar *greeter_ck_cookie;

    gboolean supports_transitions;

    /* User session process */
    Session *user_session;
    guint user_session_timer;
    PAMSession *user_pam_session;
    gchar *user_ck_cookie;

    /* Default login hint */
    gchar *default_user;
    gint timeout;

    /* Default session */
    gchar *default_session;
};

G_DEFINE_TYPE (Display, display, G_TYPE_OBJECT);

static void start_greeter (Display *display);

// FIXME: Remove the index, it is an external property
Display *
display_new (gint index)
{
    Display *self = g_object_new (DISPLAY_TYPE, NULL);

    self->priv->index = index;
    self->priv->pam_service = g_strdup (DEFAULT_PAM_SERVICE);

    return self;
}

gint
display_get_index (Display *display)
{
    return display->priv->index;
}

void
display_set_session_wrapper (Display *display, const gchar *session_wrapper)
{
    g_free (display->priv->session_wrapper);
    display->priv->session_wrapper = g_strdup (session_wrapper);  
}

const gchar *
display_get_session_wrapper (Display *display)
{
    return display->priv->session_wrapper;
}

void
display_set_default_user (Display *display, const gchar *username)
{
    g_free (display->priv->default_user);
    display->priv->default_user = g_strdup (username);
}

const gchar *
display_get_default_user (Display *display)
{
    return display->priv->default_user;
}

void
display_set_default_user_timeout (Display *display, gint timeout)
{
    display->priv->timeout = timeout;  
}

gint
display_get_default_user_timeout (Display *display)
{
    return display->priv->timeout;
}

void
display_set_greeter_user (Display *display, const gchar *username)
{
    g_free (display->priv->greeter_user);
    if (username && username[0] != '\0')
        display->priv->greeter_user = g_strdup (username);
    else
        display->priv->greeter_user = NULL;
}

const gchar *
display_get_greeter_user (Display *display)
{
    return display->priv->greeter_user;  
}

const gchar *
display_get_session_user (Display *display)
{
    if (display->priv->user_session)
        return pam_session_get_username (display->priv->user_pam_session);
    else
        return NULL;
}

void
display_set_greeter_theme (Display *display, const gchar *greeter_theme)
{
    g_free (display->priv->greeter_theme);
    display->priv->greeter_theme = g_strdup (greeter_theme);
}

const gchar *
display_get_greeter_theme (Display *display)
{
    return display->priv->greeter_theme;
}

void
display_set_default_layout (Display *display, const gchar *layout)
{
    g_free (display->priv->default_layout);
    display->priv->default_layout = g_strdup (layout);
}

const gchar *
display_get_default_layout (Display *display)
{
    return display->priv->default_layout;
}

void
display_set_default_session (Display *display, const gchar *session)
{
    g_free (display->priv->default_session);
    display->priv->default_session = g_strdup (session);
}

const gchar *
display_get_default_session (Display *display)
{
    return display->priv->default_session;
}

void
display_set_pam_service (Display *display, const gchar *service)
{
    g_free (display->priv->pam_service);
    display->priv->pam_service = g_strdup (service);
}

const gchar *
display_get_pam_service (Display *display)
{
    return display->priv->pam_service;
}

void
display_set_xserver (Display *display, XServer *xserver)
{
    if (display->priv->xserver)
        g_object_unref (display->priv->xserver);
    display->priv->xserver = g_object_ref (xserver);  
}

XServer *
display_get_xserver (Display *display)
{
    return display->priv->xserver;
}

static struct passwd *
get_user_info (const gchar *username)
{
    struct passwd *user_info;

    errno = 0;
    user_info = getpwnam (username);
    if (!user_info)
    {
        if (errno == 0)
            g_warning ("Unable to get information on user %s: User does not exist", username);
        else
            g_warning ("Unable to get information on user %s: %s", username, strerror (errno));
    }

    return user_info;
}

static void
add_int_value (GVariantBuilder *builder, const gchar *name, gint32 value)
{
    g_variant_builder_add_value (builder, g_variant_new ("(sv)", name, g_variant_new_variant (g_variant_new_int32 (value))));
}

static void
add_string_value (GVariantBuilder *builder, const gchar *name, const gchar *value)
{
    g_variant_builder_add_value (builder, g_variant_new ("(sv)", name, g_variant_new_variant (g_variant_new_string (value))));
}

static void
add_boolean_value (GVariantBuilder *builder, const gchar *name, gboolean value)
{
    g_variant_builder_add_value (builder, g_variant_new ("(sv)", name, g_variant_new_variant (g_variant_new_boolean (value))));
}

static gchar *
start_ck_session (Display *display, const gchar *session_type, const gchar *username)
{
    GDBusProxy *proxy;
    char *display_device = NULL;
    const gchar *address, *hostname = "";
    struct passwd *user_info;
    gboolean is_local = TRUE;
    GVariantBuilder *arg_builder;
    GVariant *arg0;
    GVariant *result;
    gchar *cookie = NULL;
    GError *error = NULL;

    user_info = get_user_info (username);
    if (!user_info)
        return NULL;

    if (xserver_get_vt (display->priv->xserver) >= 0)
        display_device = g_strdup_printf ("/dev/tty%d", xserver_get_vt (display->priv->xserver));
    address = xserver_get_address (display->priv->xserver);

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.ConsoleKit",
                                           "/org/freedesktop/ConsoleKit/Manager",
                                           "org.freedesktop.ConsoleKit.Manager", 
                                           NULL, NULL);
    arg_builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
    add_int_value (arg_builder, "unix-user", user_info->pw_uid);
    add_string_value (arg_builder, "session-type", session_type);
    add_string_value (arg_builder, "x11-display", address);
    if (display_device)
        add_string_value (arg_builder, "x11-display-device", display_device);
    add_string_value (arg_builder, "remote-host-name", hostname);
    add_boolean_value (arg_builder, "is-local", is_local);
    arg0 = g_variant_builder_end (arg_builder);
    result = g_dbus_proxy_call_sync (proxy,
                                     "OpenSessionWithParameters",
                                     g_variant_new_tuple (&arg0, 1),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    g_variant_builder_unref (arg_builder);
    g_object_unref (proxy);
    g_free (display_device);
    if (!result)
        g_warning ("Failed to open CK session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return NULL;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE_STRING))
        cookie = g_strdup (g_variant_get_string (result, NULL));
    g_variant_unref (result);

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

    if (g_variant_is_of_type (result, G_VARIANT_TYPE_BOOLEAN) && !g_variant_get_boolean (result))
        g_warning ("ConsoleKit.Manager.CloseSession() returned false");

    g_variant_unref (result);
}

static void
run_script (const gchar *script)
{
    // FIXME
}

static void
end_user_session (Display *display, gboolean clean_exit)
{
    run_script ("PostSession");

    g_signal_emit (display, signals[END_SESSION], 0, display->priv->user_session);
  
    if (display->priv->user_session_timer)
    {
        g_source_remove (display->priv->user_session_timer);
        display->priv->user_session_timer = 0;
    }

    g_object_unref (display->priv->user_session);
    display->priv->user_session = NULL;

    pam_session_end (display->priv->user_pam_session);
    g_object_unref (display->priv->user_pam_session);
    display->priv->user_pam_session = NULL;

    end_ck_session (display->priv->user_ck_cookie);
    g_free (display->priv->user_ck_cookie);
    display->priv->user_ck_cookie = NULL;

    if (!clean_exit)
        g_warning ("Session exited unexpectedly");

    xserver_disconnect_clients (display->priv->xserver);
}

static void
user_session_exited_cb (Session *session, gint status, Display *display)
{
    end_user_session (display, status == 0);
}

static void
user_session_killed_cb (Session *session, gint status, Display *display)
{
    end_user_session (display, FALSE);
}

static void
set_env_from_pam_session (Session *session, PAMSession *pam_session)
{
    gchar **pam_env;

    pam_env = pam_session_get_envlist (pam_session);
    if (pam_env)
    {
        int i;
        for (i = 0; pam_env[i]; i++)
        {
            g_debug ("pam_env[%d]=%s", i, pam_env[i]);
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

static void
set_env_from_keyfile (Session *session, const gchar *name, GKeyFile *key_file, const gchar *section, const gchar *key)
{
    char *value;

    value = g_key_file_get_string (key_file, section, key, NULL);
    if (!value)
        return;

    child_process_set_env (CHILD_PROCESS (session), name, value);
    g_free (value);
}

static void
start_user_session (Display *display, const gchar *session, const gchar *language)
{
    gchar *filename, *path, *old_language;
    struct passwd *user_info;
    GKeyFile *dmrc_file, *session_desktop_file;
    gboolean have_dmrc = FALSE, result;
    GError *error = NULL;

    run_script ("PreSession");

    g_debug ("Launching '%s' session for user %s", session, pam_session_get_username (display->priv->user_pam_session));
    display->priv->login_count++;

    /* Load the users login settings (~/.dmrc) */
    dmrc_file = g_key_file_new ();
    user_info = get_user_info (pam_session_get_username (display->priv->user_pam_session));
    if (user_info)
    {
        /* Load from the user directory, if this fails (e.g. the user directory
         * is not yet mounted) then load from the cache */
        path = g_build_filename (user_info->pw_dir, ".dmrc", NULL);
        have_dmrc = g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
        g_free (path);
    }

    /* If no .dmrc, then load from the cache */
    if (!have_dmrc)
    {
        filename = g_strdup_printf ("%s.dmrc", user_info->pw_name);
        path = g_build_filename (CACHE_DIR, "dmrc", filename, NULL);
        g_free (filename);
        if (!g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_KEEP_COMMENTS, &error))
            g_warning ("Failed to load .dmrc file %s: %s", path, error->message);
        g_clear_error (&error);
        g_free (path);
    }

    /* Update the .dmrc with changed settings */
    g_key_file_set_string (dmrc_file, "Desktop", "Session", session);
    old_language = g_key_file_get_string (dmrc_file, "Desktop", "Language", NULL);
    if (language && (!old_language || !g_str_equal(language, old_language)))
    {
        g_key_file_set_string (dmrc_file, "Desktop", "Language", language);
        /* We don't have advanced language checking, so reset these variables */
        g_key_file_remove_key (dmrc_file, "Desktop", "Langlist", NULL);
        g_key_file_remove_key (dmrc_file, "Desktop", "LCMess", NULL);
    }
    g_free (old_language);
    if (!g_key_file_has_key (dmrc_file, "Desktop", "Layout", NULL))
        g_key_file_set_string (dmrc_file, "Desktop", "Layout", display->priv->default_layout);

    filename = g_strdup_printf ("%s.desktop", session);
    path = g_build_filename (XSESSIONS_DIR, filename, NULL);
    g_free (filename);

    session_desktop_file = g_key_file_new ();
    result = g_key_file_load_from_file (session_desktop_file, path, G_KEY_FILE_NONE, &error);
    g_free (path);

    if (!result)
        g_warning ("Failed to load session file %s: %s:", path, error->message);
    g_clear_error (&error);

    if (result)
    {
        gchar *session_command;

        session_command = g_key_file_get_string (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
        if (!session_command)
            g_warning ("No command in session file %s", path);

        display->priv->supports_transitions = g_key_file_get_boolean (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-Supports-Transitions", NULL);

        if (session_command)
        {
            gchar *data;
            gsize length;

            if (display->priv->session_wrapper)
            {
                gchar *old_command = session_command;
                session_command = g_strdup_printf ("%s '%s'", display->priv->session_wrapper, session_command);
                g_free (old_command);
            }
            display->priv->user_session = session_new (pam_session_get_username (display->priv->user_pam_session), session_command);

            g_signal_connect (G_OBJECT (display->priv->user_session), "exited", G_CALLBACK (user_session_exited_cb), display);
            g_signal_connect (G_OBJECT (display->priv->user_session), "killed", G_CALLBACK (user_session_killed_cb), display);
            child_process_set_env (CHILD_PROCESS (display->priv->user_session), "DISPLAY", xserver_get_address (display->priv->xserver));
            if (display->priv->user_ck_cookie)
                child_process_set_env (CHILD_PROCESS (display->priv->user_session), "XDG_SESSION_COOKIE", display->priv->user_ck_cookie);
            child_process_set_env (CHILD_PROCESS (display->priv->user_session), "DESKTOP_SESSION", session); // FIXME: Apparently deprecated?
            child_process_set_env (CHILD_PROCESS (display->priv->user_session), "GDMSESSION", session); // FIXME: Not cross-desktop
            set_env_from_keyfile (display->priv->user_session, "LANG", dmrc_file, "Desktop", "Language");
            set_env_from_keyfile (display->priv->user_session, "LANGUAGE", dmrc_file, "Desktop", "Langlist");
            set_env_from_keyfile (display->priv->user_session, "LC_MESSAGES", dmrc_file, "Desktop", "LCMess");
            //child_process_set_env (CHILD_PROCESS (display->priv->user_session), "GDM_LANG", session_language); // FIXME: Not cross-desktop
            set_env_from_keyfile (display->priv->user_session, "GDM_KEYBOARD_LAYOUT", dmrc_file, "Desktop", "Layout"); // FIXME: Not cross-desktop
            set_env_from_pam_session (display->priv->user_session, display->priv->user_pam_session);

            g_signal_emit (display, signals[START_SESSION], 0, display->priv->user_session);

            session_start (display->priv->user_session, FALSE);

            data = g_key_file_to_data (dmrc_file, &length, NULL);

            /* Update the users .dmrc */
            if (user_info)
            {
                path = g_build_filename (user_info->pw_dir, ".dmrc", NULL);
                g_file_set_contents (path, data, length, NULL);
                chown (path, user_info->pw_uid, user_info->pw_gid);
                g_free (path);
            }

            /* Update the .dmrc cache */
            path = g_build_filename (CACHE_DIR, "dmrc", NULL);          
            g_mkdir_with_parents (path, 0700);
            g_free (path);
            filename = g_strdup_printf ("%s.dmrc", pam_session_get_username (display->priv->user_pam_session));
            path = g_build_filename (CACHE_DIR, "dmrc", filename, NULL);
            g_file_set_contents (path, data, length, NULL);
            g_free (path);

            g_free (data);
        }

        g_free (session_command);
    }

    g_key_file_free (session_desktop_file);
    g_key_file_free (dmrc_file);  
}

static void
start_default_session (Display *display, const gchar *session, const gchar *language)
{
    /* Don't need to check authentication, just authorize */
    if (display->priv->user_pam_session)
        pam_session_end (display->priv->user_pam_session);    
    display->priv->user_pam_session = pam_session_new (display->priv->pam_service, display->priv->default_user);
    pam_session_authorize (display->priv->user_pam_session);

    display->priv->user_ck_cookie = start_ck_session (display, "", pam_session_get_username (display->priv->user_pam_session));
    start_user_session (display, session, language);
}

static void
end_greeter_session (Display *display, gboolean clean_exit)
{  
    gboolean greeter_connected;
  
    if (display->priv->greeter_quit_timeout)
    {
        g_source_remove (display->priv->greeter_quit_timeout);
        display->priv->greeter_quit_timeout = 0;
    }

    g_signal_emit (display, signals[END_GREETER], 0, display->priv->greeter_session);

    greeter_connected = display->priv->greeter_connected;

    g_object_unref (display->priv->greeter_session);
    display->priv->greeter_session = NULL;
    display->priv->greeter_connected = FALSE;

    pam_session_end (display->priv->greeter_pam_session);
    g_object_unref (display->priv->greeter_pam_session);
    display->priv->greeter_pam_session = NULL;

    end_ck_session (display->priv->greeter_ck_cookie);
    g_free (display->priv->greeter_ck_cookie);
    display->priv->greeter_ck_cookie = NULL;

    if (!clean_exit)
        g_warning ("Greeter failed");
    else if (!greeter_connected)
        g_warning ("Greeter quit before connecting");
    else if (!display->priv->user_session)
        g_warning ("Greeter quit before session started");
    else
        return;

    // FIXME: Issue with greeter, don't want to start a new one, report error to user
}

static void
handle_connect (Display *display)
{
    gchar *theme;

    if (!display->priv->greeter_connected)
    {
        display->priv->greeter_connected = TRUE;
        g_debug ("Greeter connected");
    }

    child_process_write_int (CHILD_PROCESS (display->priv->greeter_session), GREETER_MESSAGE_CONNECTED);
    theme = g_build_filename (THEME_DIR, display->priv->greeter_theme, "index.theme", NULL);
    child_process_write_string (CHILD_PROCESS (display->priv->greeter_session), theme);
    g_free (theme);
    child_process_write_string (CHILD_PROCESS (display->priv->greeter_session), display->priv->default_layout);
    child_process_write_string (CHILD_PROCESS (display->priv->greeter_session), display->priv->default_session);
    child_process_write_string (CHILD_PROCESS (display->priv->greeter_session), display->priv->default_user ? display->priv->default_user : "");
    child_process_write_int (CHILD_PROCESS (display->priv->greeter_session), display->priv->timeout);
    child_process_flush (CHILD_PROCESS (display->priv->greeter_session));
}

static void
pam_messages_cb (PAMSession *session, int num_msg, const struct pam_message **msg, Display *display)
{
    int i;

    /* Respond to d-bus query with messages */
    g_debug ("Prompt greeter with %d message(s)", num_msg);
    child_process_write_int (CHILD_PROCESS (display->priv->greeter_session), GREETER_MESSAGE_PROMPT_AUTHENTICATION);
    child_process_write_int (CHILD_PROCESS (display->priv->greeter_session), num_msg);
    for (i = 0; i < num_msg; i++)
    {
        child_process_write_int (CHILD_PROCESS (display->priv->greeter_session), msg[i]->msg_style);
        child_process_write_string (CHILD_PROCESS (display->priv->greeter_session), msg[i]->msg);
    }
    child_process_flush (CHILD_PROCESS (display->priv->greeter_session));  
}

static void
authenticate_result_cb (PAMSession *session, int result, Display *display)
{
    g_debug ("Authenticate result for user %s: %s", pam_session_get_username (display->priv->user_pam_session), pam_session_strerror (display->priv->user_pam_session, result));

    if (result == PAM_SUCCESS)
    {
        run_script ("PostLogin");
        pam_session_authorize (session);
    }

    /* Respond to D-Bus request */
    child_process_write_int (CHILD_PROCESS (display->priv->greeter_session), GREETER_MESSAGE_END_AUTHENTICATION);
    child_process_write_int (CHILD_PROCESS (display->priv->greeter_session), result);   
    child_process_flush (CHILD_PROCESS (display->priv->greeter_session));
}

static void
session_started_cb (PAMSession *session, Display *display)
{
    display->priv->user_ck_cookie = start_ck_session (display, "", pam_session_get_username (display->priv->user_pam_session));
}

static void
handle_start_authentication (Display *display, const gchar *username)
{
    GError *error = NULL;

    if (!display->priv->greeter_session || display->priv->user_session)
        return;

    /* Abort existing authentication */
    if (display->priv->user_pam_session)
    {
        g_signal_handlers_disconnect_matched (display->priv->user_pam_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        pam_session_end (display->priv->user_pam_session);
        g_object_unref (display->priv->user_pam_session);
    }

    g_debug ("Greeter start authentication for %s", username);

    display->priv->user_pam_session = pam_session_new (display->priv->pam_service, username);
    g_signal_connect (G_OBJECT (display->priv->user_pam_session), "got-messages", G_CALLBACK (pam_messages_cb), display);
    g_signal_connect (G_OBJECT (display->priv->user_pam_session), "authentication-result", G_CALLBACK (authenticate_result_cb), display);
    g_signal_connect (G_OBJECT (display->priv->user_pam_session), "started", G_CALLBACK (session_started_cb), display);

    if (!pam_session_start (display->priv->user_pam_session, &error))
        g_warning ("Failed to start authentication: %s", error->message);
}

static void
handle_continue_authentication (Display *display, gchar **secrets)
{
    int num_messages;
    const struct pam_message **messages;
    struct pam_response *response;
    int i, j, n_secrets = 0;

    /* Not connected */
    if (!display->priv->greeter_connected)
        return;

    /* Not in authorization */
    if (display->priv->user_pam_session == NULL)
        return;

    num_messages = pam_session_get_num_messages (display->priv->user_pam_session);
    messages = pam_session_get_messages (display->priv->user_pam_session);

    /* Check correct number of responses */
    for (i = 0; i < num_messages; i++)
    {
        int msg_style = messages[i]->msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
            n_secrets++;
    }
    if (g_strv_length (secrets) != n_secrets)
    {
        pam_session_end (display->priv->user_pam_session);
        return;
    }

    g_debug ("Continue authentication");

    /* Build response */
    response = calloc (num_messages, sizeof (struct pam_response));  
    for (i = 0, j = 0; i < num_messages; i++)
    {
        int msg_style = messages[i]->msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
        {
            response[i].resp = strdup (secrets[j]); // FIXME: Need to convert from UTF-8
            j++;
        }
    }

    pam_session_respond (display->priv->user_pam_session, response);
}

static gboolean
quit_greeter_cb (gpointer data)
{
    Display *display = data;
    g_warning ("Greeter did not quit, sending kill signal");
    session_stop (display->priv->greeter_session);
    display->priv->greeter_quit_timeout = 0;
    return TRUE;
}

static void
quit_greeter (Display *display)
{
    child_process_write_int (CHILD_PROCESS (display->priv->greeter_session), GREETER_MESSAGE_QUIT);
    child_process_flush (CHILD_PROCESS (display->priv->greeter_session));

    if (display->priv->greeter_quit_timeout)
        g_source_remove (display->priv->greeter_quit_timeout);
    display->priv->greeter_quit_timeout = g_timeout_add (GREETER_QUIT_TIMEOUT, quit_greeter_cb, display);
}

static gboolean
session_timeout_cb (Display *display)
{
    g_warning ("Session has not indicated it is ready, stopping greeter anyway");

    /* Stop the greeter */
    quit_greeter (display);

    display->priv->user_session_timer = 0;
    return FALSE;
}

static void
handle_login (Display *display, gchar *username, gchar *session, gchar *language)
{
    if (display->priv->user_session != NULL)
    {
        g_warning ("Ignoring request to log in when already logged in");
        return;
    }

    g_debug ("Greeter login for user %s on session %s", username, session);
  
    /* Default session requested */
    if (strcmp (session, "") == 0)
        session = display->priv->default_session;

    /* Default language requested */
    if (strcmp (language, "") == 0)
        language = NULL;

    if (display->priv->default_user && strcmp (username, display->priv->default_user) == 0)
        start_default_session (display, session, language);
    else if (display->priv->user_pam_session &&
             pam_session_get_in_session (display->priv->user_pam_session) &&
             strcmp (username, pam_session_get_username (display->priv->user_pam_session)) == 0)
        start_user_session (display, session, language);
    else
    {
        g_warning ("Ignoring request for login with unauthenticated user");
        return;
    }

    /* Stop session, waiting for user session to indicate it is ready (if supported) */
    // FIXME: Hard-coded timeout
    if (display->priv->supports_transitions)
        display->priv->user_session_timer = g_timeout_add (USER_SESSION_TIMEOUT, (GSourceFunc) session_timeout_cb, display);
    else
        quit_greeter (display);
}

static void
greeter_data_cb (Session *session, Display *display)
{
    int message, n_secrets, i;
    gchar *username, *session_name, *language;
    gchar **secrets;

    /* FIXME: This could all block and lock up the server */

    message = child_process_read_int (CHILD_PROCESS (session));
    switch (message)
    {
    case GREETER_MESSAGE_CONNECT:
        handle_connect (display);
        break;
    case GREETER_MESSAGE_START_AUTHENTICATION:
        username = child_process_read_string (CHILD_PROCESS (session));
        handle_start_authentication (display, username);
        g_free (username);
        break;
    case GREETER_MESSAGE_CONTINUE_AUTHENTICATION:
        n_secrets = child_process_read_int (CHILD_PROCESS (session));
        secrets = g_malloc (sizeof (gchar *) * (n_secrets + 1));
        for (i = 0; i < n_secrets; i++)
            secrets[i] = child_process_read_string (CHILD_PROCESS (session));
        secrets[i] = NULL;
        handle_continue_authentication (display, secrets);
        g_strfreev (secrets);
        break;
    case GREETER_MESSAGE_LOGIN:
        username = child_process_read_string (CHILD_PROCESS (session));
        session_name = child_process_read_string (CHILD_PROCESS (session));
        language = child_process_read_string (CHILD_PROCESS (session));
        handle_login (display, username, session_name, language);
        g_free (username);
        g_free (session_name);
        g_free (language);
        break;
    default:
        g_warning ("Unknown message from greeter: %d", message);
        break;
    }
}

static void
greeter_session_exited_cb (Session *session, gint status, Display *display)
{
    end_greeter_session (display, status == 0);
}

static void
greeter_session_killed_cb (Session *session, gint status, Display *display)
{
    end_greeter_session (display, FALSE);
}

static void
start_greeter (Display *display)
{
    GKeyFile *theme;
    GError *error = NULL;
  
    theme = load_theme (display->priv->greeter_theme, &error);
    if (!theme)
        g_warning ("Failed to find theme %s: %s", display->priv->greeter_theme, error->message);
    g_clear_error (&error);

    if (theme)
    {
        gchar *command;
        gchar *username = NULL;

        g_debug ("Starting greeter %s as user %s", display->priv->greeter_theme,
                 display->priv->greeter_user ? display->priv->greeter_user : "<current>");

        command = theme_get_command (theme);
      
        if (display->priv->greeter_user)
            username = display->priv->greeter_user;
        else
        {
            struct passwd *user_info;
            user_info = getpwuid (getuid ());
            if (!user_info)
            {
                g_warning ("Unable to determine current username: %s", strerror (errno));
                return;
            }
            username = user_info->pw_name;
        }

        display->priv->greeter_pam_session = pam_session_new (display->priv->pam_service, username);
        pam_session_authorize (display->priv->greeter_pam_session);

        display->priv->greeter_ck_cookie = start_ck_session (display,
                                                              "LoginWindow",
                                                              username);

        display->priv->greeter_connected = FALSE;
        display->priv->greeter_session = session_new (username, command);
        g_signal_connect (G_OBJECT (display->priv->greeter_session), "got-data", G_CALLBACK (greeter_data_cb), display);      
        g_signal_connect (G_OBJECT (display->priv->greeter_session), "exited", G_CALLBACK (greeter_session_exited_cb), display);
        g_signal_connect (G_OBJECT (display->priv->greeter_session), "killed", G_CALLBACK (greeter_session_killed_cb), display);
        child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "DISPLAY", xserver_get_address (display->priv->xserver));
        if (display->priv->greeter_ck_cookie)
            child_process_set_env (CHILD_PROCESS (display->priv->greeter_session), "XDG_SESSION_COOKIE", display->priv->greeter_ck_cookie);
        set_env_from_pam_session (display->priv->greeter_session, display->priv->greeter_pam_session);

        g_signal_emit (display, signals[START_GREETER], 0, display->priv->greeter_session);

        session_start (display->priv->greeter_session, TRUE);

        g_free (command);
        g_key_file_free (theme);
    }
}

static void
xserver_exit_cb (XServer *server, Display *display)
{
    g_object_unref (display->priv->xserver);
    display->priv->xserver = NULL;
    g_signal_emit (display, signals[EXITED], 0);
}

static void
xserver_ready_cb (XServer *xserver, Display *display)
{
    run_script ("Init"); // FIXME: Async

    /* Don't run any sessions on local terminals */
    if (xserver_get_server_type (xserver) == XSERVER_TYPE_LOCAL_TERMINAL)
        return;

    /* If have user then automatically login the first time */
    if (display->priv->default_user && display->priv->timeout == 0 && display->priv->login_count == 0)
        start_default_session (display, display->priv->default_session, NULL);
    else
        start_greeter (display);
}

gboolean
display_start (Display *display)
{
    g_return_val_if_fail (display->priv->xserver != NULL, FALSE);
    g_signal_connect (G_OBJECT (display->priv->xserver), "ready", G_CALLBACK (xserver_ready_cb), display);
    g_signal_connect (G_OBJECT (display->priv->xserver), "exited", G_CALLBACK (xserver_exit_cb), display);
    return xserver_start (display->priv->xserver);
}

static void
display_init (Display *display)
{
    display->priv = G_TYPE_INSTANCE_GET_PRIVATE (display, DISPLAY_TYPE, DisplayPrivate);
    if (strcmp (GREETER_USER, "") != 0)
        display->priv->greeter_user = g_strdup (GREETER_USER);
    display->priv->greeter_theme = g_strdup (GREETER_THEME);
    display->priv->default_layout = g_strdup ("us"); // FIXME: Is there a better default to get?
    display->priv->default_session = g_strdup (DEFAULT_SESSION);
}

static void
display_finalize (GObject *object)
{
    Display *self;

    self = DISPLAY (object);

    if (self->priv->greeter_session)
        g_object_unref (self->priv->greeter_session);
    if (self->priv->user_session_timer)
        g_source_remove (self->priv->user_session_timer);
    if (self->priv->user_session)
        g_object_unref (self->priv->user_session);
    if (self->priv->user_pam_session)
        g_object_unref (self->priv->user_pam_session);
    end_ck_session (self->priv->greeter_ck_cookie);
    g_free (self->priv->greeter_ck_cookie);
    end_ck_session (self->priv->user_ck_cookie);
    g_free (self->priv->user_ck_cookie);
    if (self->priv->xserver)  
        g_object_unref (self->priv->xserver);
    g_free (self->priv->greeter_user);
    g_free (self->priv->greeter_theme);
    g_free (self->priv->default_user);
    g_free (self->priv->default_layout);
    g_free (self->priv->default_session);
}

static void
display_class_init (DisplayClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = display_finalize;

    g_type_class_add_private (klass, sizeof (DisplayPrivate));

    signals[START_GREETER] =
        g_signal_new ("start-greeter",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, start_greeter),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, SESSION_TYPE);

    signals[END_GREETER] =
        g_signal_new ("end-greeter",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, end_greeter),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, SESSION_TYPE);
  
    signals[START_SESSION] =
        g_signal_new ("start-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, start_session),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, SESSION_TYPE);

    signals[END_SESSION] =
        g_signal_new ("end-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, end_session),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, SESSION_TYPE);

    signals[EXITED] =
        g_signal_new ("exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, exited),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
