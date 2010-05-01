/*
 * Copyright (C) 2010 Canonical Ltd.
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
#include "pam-session.h"

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
    /* Display device */
    char *display_device; // ?
    char *x11_display_device; // e.g. /dev/tty7
    char *x11_display; // e.g. :0
  
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

    /* Session to execute */
    char *user_session;
  
    /* Active session */
    SessionType active_session;

    // FIXME: Token for secure access to this server
};

G_DEFINE_TYPE (Display, display, G_TYPE_OBJECT);

static void start_greeter (Display *display);
static void start_user_session (Display *display);

Display *
display_new (void)
{
    return g_object_new (DISPLAY_TYPE, NULL);
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
        start_greeter (display);
        break;
    case SESSION_GREETER_AUTHENTICATED:
        start_user_session (display);
        break;
    case SESSION_USER:
        pam_session_end (display->priv->pam_session);
        ck_connector_close_session (display->priv->ck_session, NULL); // FIXME: Handle errors
        ck_connector_unref (display->priv->ck_session);
        display->priv->ck_session = NULL;

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
start_session (Display *display, const char *username, const char *executable)
{
    struct passwd *user_info;
    gint session_stdin, session_stdout, session_stderr;
    gboolean result;
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
    char *env[] = { g_strdup_printf ("USER=%s", user_info->pw_name),
                    g_strdup_printf ("HOME=%s", user_info->pw_dir),
                    g_strdup_printf ("SHELL=%s", user_info->pw_shell),
                    g_strdup_printf ("HOME=%s", user_info->pw_dir),
                    g_strdup_printf ("DISPLAY=%s", display->priv->x11_display),
                    display->priv->ck_session ? g_strdup_printf ("XDG_SESSION_COOKIE=%s", ck_connector_get_cookie (display->priv->ck_session)) : NULL,
                    NULL };
    char *argv[] = { g_strdup (executable), NULL };

    result = g_spawn_async_with_pipes (user_info->pw_dir,
                                       argv,
                                       env,
                                       G_SPAWN_DO_NOT_REAP_CHILD,
                                       session_fork_cb, user_info,
                                       &display->priv->session_pid,
                                       &session_stdin, &session_stdout, &session_stderr,
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
    g_debug ("Launching session %s for user %s", display->priv->user_session, pam_session_get_username (display->priv->pam_session));

    display->priv->active_session = SESSION_USER;
    start_session (display, pam_session_get_username (display->priv->pam_session), display->priv->user_session);
}

static void
start_greeter (Display *display)
{
    g_debug ("Launching greeter %s as user %s", GREETER_BINARY, GREETER_USER);

    display->priv->active_session = SESSION_GREETER_PRE_CONNECT;
    start_session (display, GREETER_USER, GREETER_BINARY);
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
    DBusError error;
    const gchar *username;

    display->priv->active_session = SESSION_GREETER_AUTHENTICATED;

    display->priv->ck_session = ck_connector_new ();
    dbus_error_init (&error);
    username = pam_session_get_username (display->priv->pam_session);
    if (!ck_connector_open_session_with_parameters (display->priv->ck_session, &error,
                                                    "unix-user", &username,
                                                    "display-device", &display->priv->display_device,
                                                    "x11-display-device", &display->priv->x11_display_device,
                                                    "x11-display", &display->priv->x11_display,
                                                    NULL))
        g_warning ("Failed to open CK session: %s: %s", error.name, error.message);
}

gboolean
display_connect (Display *display, const char **username, gint *delay, GError *error)
{
    if (display->priv->active_session == SESSION_GREETER_PRE_CONNECT)
    {
        display->priv->active_session = SESSION_GREETER;
        g_debug ("Greeter connected");
    }

    *username = g_strdup ("");
    *delay = 0;
    return TRUE;
}

gboolean
display_start_authentication (Display *display, const char *username, DBusGMethodInvocation *context)
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

    display->priv->pam_session = pam_session_new ();
    g_signal_connect (G_OBJECT (display->priv->pam_session), "got-messages", G_CALLBACK (pam_messages_cb), display);
    g_signal_connect (G_OBJECT (display->priv->pam_session), "authentication-result", G_CALLBACK (authenticate_result_cb), display);
    g_signal_connect (G_OBJECT (display->priv->pam_session), "started", G_CALLBACK (session_started_cb), display);
    if (!pam_session_start (display->priv->pam_session, username, &error))
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
xserver_watch_cb (GPid pid, gint status, gpointer data)
{
    Display *display = data;

    if (WIFEXITED (status))
        g_debug ("Display exited with return value %d", WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        g_debug ("Display terminated with signal %d", WTERMSIG (status));

    display->priv->xserver_pid = 0;

    g_signal_emit (display, signals[EXITED], 0);
}

void
display_start (Display *display)
{
    GError *error = NULL;
    gboolean result;
    gint xserver_stdin, xserver_stdout, xserver_stderr;

    char *argv[] = { "/usr/bin/X", display->priv->x11_display, NULL };
    char *env[] = { NULL };
    result = g_spawn_async_with_pipes (NULL, /* Working directory */
                                       argv,
                                       env,
                                       G_SPAWN_DO_NOT_REAP_CHILD,
                                       NULL, NULL,
                                       &display->priv->xserver_pid,
                                       &xserver_stdin, &xserver_stdout, &xserver_stderr,
                                       &error);
    if (!result)
        g_warning ("Unable to create display: %s", error->message);
    else
        g_child_watch_add (display->priv->xserver_pid, xserver_watch_cb, display);
    g_clear_error (&error);
  
    /* TODO: Do autologin if this is requested */
    if (display->priv->xserver_pid != 0)
        start_greeter (display);
}

static void
display_init (Display *display)
{
    display->priv = G_TYPE_INSTANCE_GET_PRIVATE (display, DISPLAY_TYPE, DisplayPrivate);

    display->priv->user_session = g_strdup ("/usr/bin/xeyes");
    // FIXME: How to get these?
    display->priv->display_device = g_strdup ("");
    display->priv->x11_display_device = g_strdup ("/dev/tty0");
    display->priv->x11_display = g_strdup (":0");
}

static void
display_class_init (DisplayClass *klass)
{
    g_type_class_add_private (klass, sizeof (DisplayPrivate));

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
