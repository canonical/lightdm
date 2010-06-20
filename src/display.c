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
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <pwd.h>
#include <unistd.h>
#include <ck-connector.h>

#include "display.h"
#include "display-glue.h"
#include "xserver.h"
#include "pam-session.h"

enum {
    PROP_0,
    PROP_CONFIG,
    PROP_SESSIONS,
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

    SessionManager *sessions;
  
    gint index;

    XServer *xserver;
  
    /* X process */
    GPid xserver_pid;

    /* Session process (either greeter or user session) */
    GPid session_pid;
  
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

    // FIXME: Token for secure access to this server
};

G_DEFINE_TYPE (Display, display, G_TYPE_OBJECT);

static void start_greeter (Display *display);
static void start_user_session (Display *display);

Display *
display_new (GKeyFile *config, SessionManager *sessions, gint index)
{
    return g_object_new (DISPLAY_TYPE, "config", config, "sessions", sessions, "index", index, NULL);
}

gint
display_get_index (Display *display)
{
    return display->priv->index;
}

static void
start_session (Display *display)
{
    DBusError error;
    const gchar *username, *x11_display;

    display->priv->ck_session = ck_connector_new ();
    dbus_error_init (&error);
    username = pam_session_get_username (display->priv->pam_session);
    x11_display = xserver_get_display (display->priv->xserver);
    if (!ck_connector_open_session_with_parameters (display->priv->ck_session, &error,
                                                    "unix-user", &username,
                                                    //"display-device", &display->priv->display_device,
                                                    //"x11-display-device", &display->priv->x11_display_device,
                                                    "x11-display", &x11_display,
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
session_watch_cb (GPid pid, gint status, gpointer data)
{
    Display *display = data;
    SessionType session;
  
    session = display->priv->active_session;
    display->priv->session_pid = 0;
    display->priv->active_session = SESSION_NONE;

    switch (session)
    {
    case SESSION_NONE:
        break;
    case SESSION_GREETER_PRE_CONNECT:
    case SESSION_GREETER:
    case SESSION_GREETER_AUTHENTICATED:      
        if (WIFEXITED (status))
            g_debug ("Greeter exited with return value %d", WEXITSTATUS (status));
        else if (WIFSIGNALED (status))
            g_debug ("Greeter terminated with signal %d", WTERMSIG (status));
        break;

    case SESSION_USER:
        if (WIFEXITED (status))
            g_debug ("Session exited with return value %d", WEXITSTATUS (status));
        else if (WIFSIGNALED (status))
            g_debug ("Session terminated with signal %d", WTERMSIG (status));
        break;
    }

    // FIXME: Check for respawn loops
    switch (session)
    {
    case SESSION_NONE:
        break;
    case SESSION_GREETER_PRE_CONNECT:
        g_error ("Failed to start greeter");
        break;
    case SESSION_GREETER:
        if (display->priv->default_user[0] != '\0' && display->priv->timeout > 0)
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
session_fork_cb (gpointer data)
{
    struct passwd *user_info = data;
  
    if (setgid (user_info->pw_gid) != 0)
    {
        g_warning ("Failed to set group ID: %s", strerror (errno));
        _exit(1);
    }
    // FIXME: Is there a risk of connecting to the process for a user in the given group and accessing memory?
    if (setuid (user_info->pw_uid) != 0)
    {
        g_warning ("Failed to set user ID: %s", strerror (errno));
        _exit(1);
    }
    if (chdir (user_info->pw_dir) != 0)
        g_warning ("Failed to change directory: %s", strerror (errno));
}

static void
open_session (Display *display, const gchar *username, const gchar *command, gboolean is_greeter)
{
    struct passwd *user_info;
    gint session_stdin, session_stdout, session_stderr;
    gboolean result;
    gint argc;
    gchar **argv;
    gchar **env;
    gchar *env_string;
    gint n_env = 0;
    GError *error = NULL;

    g_return_if_fail (display->priv->session_pid == 0);

    errno = 0;
    user_info = getpwnam (username);
    if (!user_info)
    {
        if (errno == 0)
            g_warning ("Unable to get information on user %s: User does not exist", username);
        else
            g_warning ("Unable to get information on user %s: %s", username, strerror (errno));
        return;
    }

    // FIXME: Do these need to be freed?
    env = g_malloc (sizeof (gchar *) * 10);
    env[n_env++] = g_strdup_printf ("USER=%s", user_info->pw_name);
    env[n_env++] = g_strdup_printf ("HOME=%s", user_info->pw_dir);
    env[n_env++] = g_strdup_printf ("SHELL=%s", user_info->pw_shell);
    env[n_env++] = g_strdup_printf ("HOME=%s", user_info->pw_dir);
    env[n_env++] = g_strdup_printf ("DISPLAY=%s", xserver_get_display (display->priv->xserver));
    if (is_greeter)
    {
        // FIXME: D-Bus not known about in here!
        //env[n_env++] = g_strdup_printf ("DBUS_SESSION_BUS_ADDRESS=%s", getenv ("DBUS_SESSION_BUS_ADDRESS")); // FIXME: Only if using session bus
        //env[n_env++] = g_strdup ("LDM_BUS=SESSION"); // FIXME: Only if using session bus
        env[n_env++] = g_strdup_printf ("LDM_DISPLAY=/org/gnome/LightDisplayManager/Display%d", display->priv->index);
    }
    if (display->priv->ck_session)
        env[n_env++] = g_strdup_printf ("XDG_SESSION_COOKIE=%s", ck_connector_get_cookie (display->priv->ck_session));
    env[n_env] = NULL;
    result = g_shell_parse_argv (command, &argc, &argv, &error);
    if (!result)
        g_error ("Failed to parse session command line: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    env_string = g_strjoinv (" ", env);
    g_debug ("Launching greeter: %s %s", env_string, command);
    g_free (env_string);

    result = g_spawn_async/*_with_pipes*/ (user_info->pw_dir,
                                       argv,
                                       env,
                                       G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                       session_fork_cb, user_info,
                                       &display->priv->session_pid,
                                       //&session_stdin, &session_stdout, &session_stderr,
                                       &error);

    if (!result)
        g_warning ("Failed to spawn session: %s", error->message);
    else
        g_child_watch_add (display->priv->session_pid, session_watch_cb, display);
    g_clear_error (&error);
}

static void
start_user_session (Display *display)
{
    Session *session;

    g_debug ("Launching %s session for user %s", display->priv->session_name, pam_session_get_username (display->priv->pam_session));

    session = session_manager_get_session (display->priv->sessions, display->priv->session_name);
    g_return_if_fail (session != NULL);

    display->priv->active_session = SESSION_USER;
    open_session (display, pam_session_get_username (display->priv->pam_session), session->exec, FALSE);
}

static GKeyFile *
load_theme (const gchar *name, GError **error)
{
    gchar *filename, *path;
    GKeyFile *theme;
    gboolean result;

    filename = g_strdup_printf ("%s.theme", name);
    path = g_build_filename (THEME_DIR, filename, NULL);
    g_free (filename);

    theme = g_key_file_new ();
    result = g_key_file_load_from_file (theme, path, G_KEY_FILE_NONE, error);
    g_free (path);

    if (!result)
    {
        g_key_file_free (theme);
        return NULL;
    }

    return theme;
}

static gchar *
theme_get_command (GKeyFile *theme)
{
    gchar *engine, *command = NULL;

    engine = g_key_file_get_value (theme, "theme", "engine", NULL);
    if (!engine)
    {
        g_warning ("No engine defined in theme");
        return NULL;
    }

    if (strcmp (engine, "gtk") == 0)
        command = g_build_filename (THEME_ENGINE_DIR, "ldm-gtk-greeter", NULL);
    else if (strcmp (engine, "webkit") == 0)
    {
        gchar *binary, *url;

        binary = g_build_filename (THEME_ENGINE_DIR, "ldm-webkit-greeter", NULL);
        url = g_key_file_get_value (theme, "theme", "url", NULL);
        if (url)
        {
            if (strchr (url, ':'))
                command = g_strdup_printf ("%s %s", binary, url);
            else
                command = g_strdup_printf ("%s file://%s/%s", binary, THEME_DIR, url);
        }
        else
            g_warning ("Missing URL in WebKit theme");
        g_free (binary);
        g_free (url);
    }
    else if (strcmp (engine, "custom") == 0)
    {
        command = g_key_file_get_value (theme, "theme", "command", NULL);
        if (!command)
            g_warning ("Missing command in custom theme");
    }
    else
        g_warning ("Unknown theme engine: %s", engine);

    return command;
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

void
display_start (Display *display, const gchar *session, const gchar *username, gint timeout)
{
    display->priv->xserver = xserver_new (display->priv->config, display->priv->index);
    g_signal_connect (G_OBJECT (display->priv->xserver), "exited", G_CALLBACK (xserver_exit_cb), display);
    if (!xserver_start (display->priv->xserver))
        return;

    display->priv->session_name = g_strdup (session);
    display->priv->default_user = g_strdup (username ? username : "");
    display->priv->timeout = timeout;

    if (username && timeout == 0)
    {
        display->priv->pam_session = pam_session_new (username);
        pam_session_authorize (display->priv->pam_session);
        start_session (display);
        start_user_session (display);
    }
    else
        start_greeter (display);
}

static void
display_init (Display *display)
{
    display->priv = G_TYPE_INSTANCE_GET_PRIVATE (display, DISPLAY_TYPE, DisplayPrivate);
    display->priv->session_name = g_strdup (DEFAULT_SESSION);
}

static void
display_set_property(GObject      *object,
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
        if (session)
        {
            g_free (self->priv->session_name);
            self->priv->session_name = session;
        }
        break;
    case PROP_SESSIONS:
        self->priv->sessions = g_object_ref (g_value_get_object (value));
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
display_get_property(GObject    *object,
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
    case PROP_SESSIONS:
        g_value_set_object (value, self->priv->sessions);
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
display_class_init (DisplayClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = display_set_property;
    object_class->get_property = display_get_property;

    g_type_class_add_private (klass, sizeof (DisplayPrivate));

    g_object_class_install_property (object_class,
                                     PROP_CONFIG,
                                     g_param_spec_pointer ("config",
                                                           "config",
                                                           "Configuration",
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_SESSIONS,
                                     g_param_spec_object ("sessions",
                                                          "sessions",
                                                          "Sessions available",
                                                          SESSION_MANAGER_TYPE,
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
