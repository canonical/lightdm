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

#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <ck-connector.h>
#include <gio/gdesktopappinfo.h>

#include "display.h"
#include "display-glue.h"
#include "session.h"
#include "pam-session.h"
#include "theme.h"

enum {
    PROP_0,
    PROP_CONFIG,
    PROP_INDEX
};

enum {
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
    GKeyFile *config;

    gint index;

    /* X server */
    XServer *xserver;

    /* Session process (either greeter or user session) */
    Session *session;

    /* Current D-Bus call context */
    DBusGMethodInvocation *dbus_context;

    /* PAM session */
    PAMSession *pam_session;
  
    /* ConsoleKit session */
    CkConnector *ck_session;

    /* Default login hint */
    gchar *default_user;
    gint timeout;

    /* Session to execute */
    gchar *session_name;

    /* Active session */
    SessionType active_session;
};

G_DEFINE_TYPE (Display, display, G_TYPE_OBJECT);

static void start_greeter (Display *display);
static void start_user_session (Display *display);

Display *
display_new (GKeyFile *config, gint index)
{
    return g_object_new (DISPLAY_TYPE, "config", config, "index", index, NULL);
}

gint
display_get_index (Display *display)
{
    return display->priv->index;
}

XServer *
display_get_xserver (Display *display)
{
    return display->priv->xserver;
}

static void
start_session (Display *display)
{
    DBusError error;
    const gchar *username, *address;

    display->priv->ck_session = ck_connector_new ();
    dbus_error_init (&error);
    username = pam_session_get_username (display->priv->pam_session);
    address = xserver_get_address (display->priv->xserver);
    if (!ck_connector_open_session_with_parameters (display->priv->ck_session, &error,
                                                    "unix-user", &username,
                                                    //"display-device", &display->priv->display_device,
                                                    //"x11-display-device", &display->priv->x11_display_device,
                                                    "x11-display", &address,
                                                    NULL))
        g_warning ("Failed to open CK session: %s: %s", error.name, error.message);
}

static void
end_session (Display *display)
{
    pam_session_end (display->priv->pam_session);
    g_object_unref (display->priv->pam_session);
    display->priv->pam_session = NULL;

    ck_connector_close_session (display->priv->ck_session, NULL); // FIXME: Handle errors
    ck_connector_unref (display->priv->ck_session);
    display->priv->ck_session = NULL;
}

static void
session_exit_cb (Session *session, Display *display)
{
    SessionType active_session;

    g_object_unref (display->priv->session);
    display->priv->session = NULL;

    active_session = display->priv->active_session;
    display->priv->active_session = SESSION_NONE;

    // FIXME: Check for respawn loops
    switch (active_session)
    {
    case SESSION_NONE:
        break;
    case SESSION_GREETER_PRE_CONNECT:
        g_error ("Failed to start greeter");
        break;
    case SESSION_GREETER:
        if (display->priv->default_user && display->priv->timeout > 0)
        {
            g_debug ("Starting session for default user %s", display->priv->default_user);
            display->priv->pam_session = pam_session_new (display->priv->default_user);
            pam_session_authorize (display->priv->pam_session);
            start_session (display);
            start_user_session (display);
        }
        else
            start_greeter (display);
        break;
    case SESSION_GREETER_AUTHENTICATED:
        start_user_session (display);
        break;
    case SESSION_USER:
        end_session (display);
        start_greeter (display);
        break;
    }
}
 
static void
open_session (Display *display, const gchar *username, const gchar *command, gboolean is_greeter)
{
    g_return_if_fail (display->priv->session == NULL);

    display->priv->session = session_new (display->priv->config, username, command);
    g_signal_connect (G_OBJECT (display->priv->session), "exited", G_CALLBACK (session_exit_cb), display);
    session_set_env (display->priv->session, "DISPLAY", xserver_get_address (display->priv->xserver));
    if (is_greeter)
    {
        gchar *string;

        // FIXME: D-Bus not known about in here!
        //session_set_env (display->priv->session, "DBUS_SESSION_BUS_ADDRESS", getenv ("DBUS_SESSION_BUS_ADDRESS")); // FIXME: Only if using session bus
        //session_set_env (display->priv->session, "LDM_BUS, ""SESSION"); // FIXME: Only if using session bus
        string = g_strdup_printf ("/org/gnome/LightDisplayManager/Display%d", display->priv->index);
        session_set_env (display->priv->session, "LDM_DISPLAY", string);
        g_free (string);
    }
    if (display->priv->ck_session)
        session_set_env (display->priv->session, "XDG_SESSION_COOKIE", ck_connector_get_cookie (display->priv->ck_session));

    session_start (display->priv->session);
}

static void
start_user_session (Display *display)
{
    gchar *filename, *path;
    GKeyFile *key_file;
    gboolean result;
    GError *error = NULL;

    g_debug ("Launching %s session for user %s", display->priv->session_name, pam_session_get_username (display->priv->pam_session));

    filename = g_strdup_printf ("%s.desktop", display->priv->session_name);
    path = g_build_filename (XSESSIONS_DIR, filename, NULL);
    g_free (filename);

    key_file = g_key_file_new ();
    result = g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &error);
    g_free (path);

    if (!result)
        g_warning ("Failed to load session file %s: %s:", path, error->message);
    g_clear_error (&error);

    if (result)
    {
        GDesktopAppInfo *desktop_file;

        desktop_file = g_desktop_app_info_new_from_keyfile (key_file);

        display->priv->active_session = SESSION_USER;
        open_session (display,
                      pam_session_get_username (display->priv->pam_session),
                      g_app_info_get_executable (G_APP_INFO (desktop_file)),
                      FALSE);

        g_object_unref (desktop_file);
    }

    g_key_file_free (key_file);
}

static void
start_greeter (Display *display)
{
    gchar *user, *theme_name;
    GKeyFile *theme;
    GError *error = NULL;
  
    user = g_key_file_get_value (display->priv->config, "Greeter", "user", NULL);
    if (!user || !getpwnam (user))
    {
        g_free (user);
        user = g_strdup (GREETER_USER);
    }
    theme_name = g_key_file_get_value (display->priv->config, "Greeter", "theme", NULL);
    if (!theme_name)
        theme_name = g_strdup (GREETER_THEME);

    theme = load_theme (theme_name, &error);
    if (!theme)
        g_warning ("Failed to find theme %s: %s", theme_name, error->message);
    g_clear_error (&error);

    if (theme)
    {
        gchar *command;

        g_debug ("Starting greeter %s as user %s", theme_name, user);
        display->priv->active_session = SESSION_GREETER_PRE_CONNECT;

        command = theme_get_command (theme);
        open_session (display, user, command, TRUE);
        g_free (command);
        g_key_file_free (theme);
    }

    g_free (user);
    g_free (theme_name);
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

    g_debug ("Authenticate result for user %s: %s", pam_session_get_username (display->priv->pam_session), pam_session_strerror (display->priv->pam_session, result));

    /* Respond to D-Bus request */
    request = g_ptr_array_new ();
    context = display->priv->dbus_context;
    display->priv->dbus_context = NULL;

    dbus_g_method_return (context, result, request);
}

static void
session_started_cb (PAMSession *session, Display *display)
{
    display->priv->active_session = SESSION_GREETER_AUTHENTICATED;
    start_session (display);
}

gboolean
display_connect (Display *display, const gchar **session, const gchar **username, gint *delay, GError *error)
{
    if (display->priv->active_session == SESSION_GREETER_PRE_CONNECT)
    {
        display->priv->active_session = SESSION_GREETER;
        g_debug ("Greeter connected");
    }

    *session = g_strdup (display->priv->session_name);
    *username = g_strdup (display->priv->default_user);
    *delay = display->priv->timeout;
    return TRUE;
}

gboolean
display_set_session (Display *display, const gchar *session, GError *error)
{
    g_debug ("Session set to %s", session);
    display->priv->session_name = g_strdup (session);
    return TRUE;
}

gboolean
display_start_authentication (Display *display, const gchar *username, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    if (display->priv->active_session != SESSION_GREETER)
    {
        dbus_g_method_return_error (context, NULL);
        return TRUE;
    }

    g_debug ("Greeter start authorisation for %s", username);

    // FIXME: Only allow calls from the correct greeter

    /* Store D-Bus request to respond to */
    display->priv->dbus_context = context;

    display->priv->pam_session = pam_session_new (username);
    g_signal_connect (G_OBJECT (display->priv->pam_session), "got-messages", G_CALLBACK (pam_messages_cb), display);
    g_signal_connect (G_OBJECT (display->priv->pam_session), "authentication-result", G_CALLBACK (authenticate_result_cb), display);
    g_signal_connect (G_OBJECT (display->priv->pam_session), "started", G_CALLBACK (session_started_cb), display);
    if (!pam_session_start (display->priv->pam_session, &error))
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
    if (display->priv->active_session != SESSION_GREETER)
    {
        dbus_g_method_return_error (context, NULL);
        return TRUE;
    }

    /* Not in authorization */
    if (display->priv->pam_session == NULL)
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

    num_messages = pam_session_get_num_messages (display->priv->pam_session);
    messages = pam_session_get_messages (display->priv->pam_session);

    /* Check correct number of responses */
    for (i = 0; i < num_messages; i++)
    {
        int msg_style = messages[i]->msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
            n_secrets++;
    }
    if (g_strv_length (secrets) != n_secrets)
    {
        pam_session_end (display->priv->pam_session);
        // FIXME: Throw error
        return FALSE;
    }

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
    pam_session_respond (display->priv->pam_session, response);

    return TRUE;
}

static void
xserver_exit_cb (XServer *server, Display *display)
{
    g_signal_emit (server, signals[EXITED], 0);  
}

static void
xserver_ready_cb (XServer *xserver, Display *display)
{
    /* If have user then automatically login */
    if (display->priv->default_user && display->priv->timeout == 0)
    {
        display->priv->pam_session = pam_session_new (display->priv->default_user);
        pam_session_authorize (display->priv->pam_session);
        start_session (display);
        start_user_session (display);
    }
    else
        start_greeter (display);
}

void
display_start (Display *display, const gchar *hostname, guint display_number, const gchar *username, gint timeout)
{
    display->priv->default_user = g_strdup (username);
    display->priv->timeout = timeout;

    display->priv->xserver = xserver_new (display->priv->config, hostname, display_number);
    g_signal_connect (G_OBJECT (display->priv->xserver), "ready", G_CALLBACK (xserver_ready_cb), display);
    g_signal_connect (G_OBJECT (display->priv->xserver), "exited", G_CALLBACK (xserver_exit_cb), display);
    if (!xserver_start (display->priv->xserver))
        return;
}

static void
display_init (Display *display)
{
    display->priv = G_TYPE_INSTANCE_GET_PRIVATE (display, DISPLAY_TYPE, DisplayPrivate);
    display->priv->session_name = g_strdup (DEFAULT_SESSION);
}

static void
display_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
    Display *self;
    gchar *session;

    self = DISPLAY (object);

    switch (prop_id) {
    case PROP_CONFIG:
        self->priv->config = g_value_get_pointer (value);
        session = g_key_file_get_value (self->priv->config, "LightDM", "session", NULL);
        if (!session)
            session = g_strdup (DEFAULT_SESSION);        
        break;
    case PROP_INDEX:
        self->priv->index = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
display_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
    Display *self;

    self = DISPLAY (object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_pointer (value, self->priv->config);
        break;
    case PROP_INDEX:
        g_value_set_int (value, self->priv->index);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
display_finalize (GObject *object)
{
    Display *self;

    self = DISPLAY (object);

    if (self->priv->session)
        g_object_unref (self->priv->session);
    if (self->priv->pam_session)
        g_object_unref (self->priv->pam_session);
    if (self->priv->ck_session)
        ck_connector_unref (self->priv->ck_session);
    if (self->priv->xserver)  
        g_object_unref (self->priv->xserver);
    g_free (self->priv->default_user);
    g_free (self->priv->session_name);
}

static void
display_class_init (DisplayClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = display_set_property;
    object_class->get_property = display_get_property;
    object_class->finalize = display_finalize;

    g_type_class_add_private (klass, sizeof (DisplayPrivate));

    g_object_class_install_property (object_class,
                                     PROP_CONFIG,
                                     g_param_spec_pointer ("config",
                                                           "config",
                                                           "Configuration",
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_INDEX,
                                     g_param_spec_int ("index",
                                                       "index",
                                                       "Index for this display",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    signals[EXITED] =
        g_signal_new ("exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayClass, exited),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    dbus_g_object_type_install_info (DISPLAY_TYPE, &dbus_glib_display_object_info);
}
