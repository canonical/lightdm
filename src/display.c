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

#ifdef HAVE_CONSOLE_KIT
#include <ck-connector.h>
#endif

#include "display.h"
#include "display-glue.h"
#include "pam-session.h"
#include "theme.h"
#include "ldm-marshal.h"

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
    QUIT_GREETER,
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

    /* Language to use in greeter/sessions */
    gchar *default_language;

    /* Layout to use in greeter/sessions */
    gchar *default_layout;

    /* User to run greeter as */
    gchar *greeter_user;

    /* Theme to use */
    gchar *greeter_theme;

    /* Greeter session process */
    Session *greeter_session;
    gboolean greeter_connected;
    guint greeter_quit_timeout;
    PAMSession *greeter_pam_session;
#ifdef HAVE_CONSOLE_KIT
    CkConnector *greeter_ck_session;
#endif

    gboolean supports_transitions;

    /* User session process */
    Session *user_session;
    guint user_session_timer;
    PAMSession *user_pam_session;
#ifdef HAVE_CONSOLE_KIT
    CkConnector *user_ck_session;
#endif

    /* Current D-Bus call context */
    DBusGMethodInvocation *dbus_context;

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

    return self;
}

gint
display_get_index (Display *display)
{
    return display->priv->index;
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
    display->priv->greeter_user = g_strdup (username);
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
display_set_default_language (Display *display, const gchar *language)
{
    g_free (display->priv->default_language);
    display->priv->default_language = g_strdup (language);
}

const gchar *
display_get_default_language (Display *display)
{
    return display->priv->default_language;
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

#ifdef HAVE_CONSOLE_KIT
static CkConnector *
start_ck_session (Display *display, const gchar *session_type, const gchar *username)
{
    CkConnector *session;
    DBusError error;
    const gchar *address, *hostname = "";
    struct passwd *user_info;
    gboolean is_local = TRUE;

    session = ck_connector_new ();

    user_info = get_user_info (username);
    if (!user_info)
        return session;

    dbus_error_init (&error);
    address = xserver_get_address (display->priv->xserver);
    if (!ck_connector_open_session_with_parameters (session, &error,
                                                    "session-type", &session_type,
                                                    "unix-user", &user_info->pw_uid,
                                                    //"display-device", &display->priv->display_device,
                                                    //"x11-display-device", &display->priv->x11_display_device,
                                                    "remote-host-name", &hostname,
                                                    "x11-display", &address,
                                                    "is-local", &is_local,
                                                    NULL))
        g_warning ("Failed to open CK session: %s: %s", error.name, error.message);

    return session;
}

static void
end_ck_session (CkConnector *session)
{
    if (!session)
        return;
    ck_connector_close_session (session, NULL); // FIXME: Handle errors
    ck_connector_unref (session);
}

#endif

static void
end_user_session (Display *display, gboolean clean_exit)
{  
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

#ifdef HAVE_CONSOLE_KIT
    end_ck_session (display->priv->user_ck_session);
    display->priv->user_ck_session = NULL;
#endif

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
start_user_session (Display *display, const gchar *session, const gchar *language)
{
    gchar *filename, *path;
    struct passwd *user_info;
    GKeyFile *dmrc_file, *session_desktop_file;
    gboolean have_dmrc = FALSE, result;
    GError *error = NULL;

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
        have_dmrc = g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_NONE, NULL);
        g_free (path);
    }

    /* If no .dmrc, then load from the cache */
    if (!have_dmrc)
    {
        filename = g_strdup_printf ("%s.dmrc", user_info->pw_name);
        path = g_build_filename (CACHE_DIR, "dmrc", filename, NULL);
        g_free (filename);
        if (!g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_NONE, &error))          
            g_warning ("Failed to load .dmrc file %s: %s", path, error->message);
        g_clear_error (&error);
        g_free (path);
    }
  
    /* Update the .dmrc with changed settings */
    g_key_file_set_string (dmrc_file, "Desktop", "Session", session);
    if (language)
        g_key_file_set_string (dmrc_file, "Desktop", "Language", language);
    else if (!g_key_file_has_key (dmrc_file, "Desktop", "Language", NULL))
        g_key_file_set_string (dmrc_file, "Desktop", "Language", display->priv->default_language);
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
        gchar *session_command, *command = NULL;

        session_command = g_key_file_get_string (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
        if (!session_command)
            g_warning ("No command in session file %s", path);
        if (session_command)
            command = g_strdup_printf ("/etc/X11/Xsession %s", session_command);
        g_free (session_command);

        display->priv->supports_transitions = g_key_file_get_boolean (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-Supports-Transitions", NULL);

        if (command)
        {
            gchar *session_language, *layout;
            gchar *data;
            gsize length;

            display->priv->user_session = session_new (pam_session_get_username (display->priv->user_pam_session), command);

            session_language = g_key_file_get_string (dmrc_file, "Desktop", "Language", NULL);
            g_debug ("session_language='%s'", session_language);

            layout = g_key_file_get_string (dmrc_file, "Desktop", "Layout", NULL);

            g_signal_connect (G_OBJECT (display->priv->user_session), "exited", G_CALLBACK (user_session_exited_cb), display);
            g_signal_connect (G_OBJECT (display->priv->user_session), "killed", G_CALLBACK (user_session_killed_cb), display);
            session_set_env (display->priv->user_session, "DISPLAY", xserver_get_address (display->priv->xserver));
#ifdef HAVE_CONSOLE_KIT
            session_set_env (display->priv->user_session, "XDG_SESSION_COOKIE", ck_connector_get_cookie (display->priv->user_ck_session));
#endif
            session_set_env (display->priv->user_session, "DESKTOP_SESSION", session); // FIXME: Apparently deprecated?
            session_set_env (display->priv->user_session, "GDMSESSION", session); // FIXME: Not cross-desktop
            session_set_env (display->priv->user_session, "PATH", "/usr/local/bin:/usr/bin:/bin");
            session_set_env (display->priv->user_session, "LANG", session_language);
            session_set_env (display->priv->user_session, "GDM_LANG", session_language); // FIXME: Not cross-desktop
            session_set_env (display->priv->user_session, "GDM_KEYBOARD_LAYOUT", layout); // FIXME: Not cross-desktop

            g_signal_emit (display, signals[START_SESSION], 0, display->priv->user_session);

            session_start (display->priv->user_session);

            data = g_key_file_to_data (dmrc_file, &length, NULL);

            /* Update the users .dmrc */
            if (user_info)
            {
                path = g_build_filename (user_info->pw_dir, ".dmrc", NULL);
                g_file_set_contents (path, data, length, NULL);
                g_free (path);
            }

            /* Update the .dmrc cache */
            filename = g_strdup_printf ("%s.dmrc", pam_session_get_username (display->priv->user_pam_session));
            path = g_build_filename (CACHE_DIR, "dmrc", filename, NULL);
            g_file_set_contents (path, data, length, NULL);
            g_free (path);

            g_free (data);
            g_free (session_language); 
            g_free (layout);         
        }

        g_free (command);
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
    display->priv->user_pam_session = pam_session_new (display->priv->default_user);
    pam_session_authorize (display->priv->user_pam_session);

#ifdef HAVE_CONSOLE_KIT
    display->priv->user_ck_session = start_ck_session (display, "", pam_session_get_username (display->priv->user_pam_session));
#endif
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

#ifdef HAVE_CONSOLE_KIT
    end_ck_session (display->priv->greeter_ck_session);
    display->priv->greeter_ck_session = NULL;
#endif

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

        g_debug ("Starting greeter %s as user %s", display->priv->greeter_theme,
                 display->priv->greeter_user ? display->priv->greeter_user : "<current>");

        command = theme_get_command (theme);

        display->priv->greeter_pam_session = pam_session_new (display->priv->greeter_user);
        pam_session_authorize (display->priv->greeter_pam_session);

#ifdef HAVE_CONSOLE_KIT
        display->priv->greeter_ck_session = start_ck_session (display,
                                                              "LoginWindow",
                                                              display->priv->greeter_user ? display->priv->greeter_user : getenv ("USER"));
#endif

        display->priv->greeter_connected = FALSE;
        display->priv->greeter_session = session_new (display->priv->greeter_user, command);
        g_signal_connect (G_OBJECT (display->priv->greeter_session), "exited", G_CALLBACK (greeter_session_exited_cb), display);
        g_signal_connect (G_OBJECT (display->priv->greeter_session), "killed", G_CALLBACK (greeter_session_killed_cb), display);
        session_set_env (display->priv->greeter_session, "DISPLAY", xserver_get_address (display->priv->xserver));
#ifdef HAVE_CONSOLE_KIT
        session_set_env (display->priv->greeter_session, "XDG_SESSION_COOKIE", ck_connector_get_cookie (display->priv->greeter_ck_session));
#endif

        g_signal_emit (display, signals[START_GREETER], 0, display->priv->greeter_session);

        session_start (display->priv->greeter_session);

        g_free (command);
        g_key_file_free (theme);
    }
}

#define TYPE_MESSAGE dbus_g_type_get_struct ("GValueArray", G_TYPE_INT, G_TYPE_STRING, G_TYPE_INVALID)

static void
pam_messages_cb (PAMSession *session, int num_msg, const struct pam_message **msg, Display *display)
{
    GPtrArray *request;
    int i;
    DBusGMethodInvocation *context;

    /* Respond to d-bus query with messages */
    request = g_ptr_array_new ();
    for (i = 0; i < num_msg; i++)
    {
        GValue value = { 0 };
      
        g_value_init (&value, TYPE_MESSAGE);
        g_value_take_boxed (&value, dbus_g_type_specialized_construct (TYPE_MESSAGE));
        // FIXME: Need to convert to UTF-8
        dbus_g_type_struct_set (&value, 0, msg[i]->msg_style, 1, msg[i]->msg, G_MAXUINT);
        g_ptr_array_add (request, g_value_get_boxed (&value));
    }

    context = display->priv->dbus_context;
    display->priv->dbus_context = NULL;
    dbus_g_method_return (context, 0, request);
}

static void
authenticate_result_cb (PAMSession *session, int result, Display *display)
{
    GPtrArray *request;
    DBusGMethodInvocation *context;

    g_debug ("Authenticate result for user %s: %s", pam_session_get_username (display->priv->user_pam_session), pam_session_strerror (display->priv->user_pam_session, result));

    /* Respond to D-Bus request */
    request = g_ptr_array_new ();
    context = display->priv->dbus_context;
    display->priv->dbus_context = NULL;

    dbus_g_method_return (context, result, request);
}

static void
session_started_cb (PAMSession *session, Display *display)
{
#ifdef HAVE_CONSOLE_KIT
    display->priv->user_ck_session = start_ck_session (display, "", pam_session_get_username (display->priv->user_pam_session));
#endif
}

gboolean
display_connect (Display *display,
                 const gchar **theme,
                 const gchar **language, const gchar **layout, const gchar **session,
                 const gchar **username, gint *delay, GError *error)
{
    if (!display->priv->greeter_connected)
    {
        display->priv->greeter_connected = TRUE;
        g_debug ("Greeter connected");
    }

    *theme = g_build_filename (THEME_DIR, display->priv->greeter_theme, "index.theme", NULL);
    *language = g_strdup (display->priv->default_language);
    *layout = g_strdup (display->priv->default_layout);
    *session = g_strdup (display->priv->default_session);
    *username = g_strdup (display->priv->default_user);
    *delay = display->priv->timeout;

    return TRUE;
}

gboolean
display_start_authentication (Display *display, const gchar *username, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    // FIXME: Only allow calls from the correct greeter

    if (!display->priv->greeter_session || display->priv->user_session)
    {
        dbus_g_method_return_error (context, NULL);
        return TRUE;
    }

    /* Abort existing authentication */
    if (display->priv->user_pam_session)
    {
        g_signal_handlers_disconnect_matched (display->priv->user_pam_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, display);
        pam_session_end (display->priv->user_pam_session);
        if (display->priv->dbus_context)
            dbus_g_method_return_error (display->priv->dbus_context, NULL);

        g_object_unref (display->priv->user_pam_session);
    }

    g_debug ("Greeter start authentication for %s", username);

    /* Store D-Bus request to respond to */
    display->priv->dbus_context = context;

    display->priv->user_pam_session = pam_session_new (username);
    g_signal_connect (G_OBJECT (display->priv->user_pam_session), "got-messages", G_CALLBACK (pam_messages_cb), display);
    g_signal_connect (G_OBJECT (display->priv->user_pam_session), "authentication-result", G_CALLBACK (authenticate_result_cb), display);
    g_signal_connect (G_OBJECT (display->priv->user_pam_session), "started", G_CALLBACK (session_started_cb), display);

    if (!pam_session_start (display->priv->user_pam_session, &error))
    {
        g_warning ("Failed to start authentication: %s", error->message);
        display->priv->dbus_context = NULL;
        dbus_g_method_return_error (context, NULL);
        return FALSE;
    }
    g_clear_error (&error);

    return TRUE;
}

gboolean
display_continue_authentication (Display *display, gchar **secrets, DBusGMethodInvocation *context)
{
    int num_messages;
    const struct pam_message **messages;
    struct pam_response *response;
    int i, j, n_secrets = 0;

    /* Not connected */
    if (!display->priv->greeter_connected)
    {
        dbus_g_method_return_error (context, NULL);
        return TRUE;
    }

    /* Not in authorization */
    if (display->priv->user_pam_session == NULL)
    {
        dbus_g_method_return_error (context, NULL);
        return TRUE;
    }

    /* Already in another call */
    if (display->priv->dbus_context != NULL)
    {
        dbus_g_method_return_error (context, NULL);
        return TRUE;
    }

    // FIXME: Only allow calls from the correct greeter

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
        // FIXME: Throw error
        return FALSE;
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

    display->priv->dbus_context = context;
    pam_session_respond (display->priv->user_pam_session, response);

    return TRUE;
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
    g_signal_emit (display, signals[QUIT_GREETER], 0);
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

gboolean
display_login (Display *display, gchar *username, gchar *session, gchar *language, GError *error)
{
    if (display->priv->user_session != NULL)
    {
        g_warning ("Ignoring request to log in when already logged in");
        return TRUE;
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
        return FALSE;
    }

    /* Stop session, waiting for user session to indicate it is ready (if supported) */
    // FIXME: Hard-coded timeout
    if (display->priv->supports_transitions)
        display->priv->user_session_timer = g_timeout_add (USER_SESSION_TIMEOUT, (GSourceFunc) session_timeout_cb, display);
    else
        quit_greeter (display);

    return TRUE;
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
    display->priv->greeter_user = g_strdup (GREETER_USER);
    display->priv->greeter_theme = g_strdup (GREETER_THEME);
    display->priv->default_language = getenv ("LANG") ? getenv ("LANG") : g_strdup ("C");
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
#ifdef HAVE_CONSOLE_KIT
    end_ck_session (self->priv->greeter_ck_session);
    end_ck_session (self->priv->user_ck_session);
#endif
    if (self->priv->xserver)  
        g_object_unref (self->priv->xserver);
    g_free (self->priv->greeter_user);
    g_free (self->priv->greeter_theme);
    g_free (self->priv->default_user);
    g_free (self->priv->default_language);
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

    signals[QUIT_GREETER] =
        g_signal_new ("quit_greeter",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, quit_greeter),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    dbus_g_object_type_install_info (DISPLAY_TYPE, &dbus_glib_display_object_info);
}
