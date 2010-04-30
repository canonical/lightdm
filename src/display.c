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

#include "display.h"
#include "display-glue.h"
#include "pam-authenticator.h"

enum {
    EXITED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

// FIXME: PAM sessions
// FIXME: CK sessions

struct DisplayPrivate
{
    /* X process */
    GPid xserver_pid;

    /* Session process (either greeter or user session) */
    GPid session_pid;
  
    /* Current D-Bus call context */
    DBusGMethodInvocation *dbus_context;

    /* Authentication handle */
    PAMAuthenticator *authenticator;

    /* User logged in as */
    char *username;

    /* TRUE if user has been authenticated */
    gboolean authenticated;

    /* Session to execute */
    char *user_session;
  
    /* TRUE if in user session */
    gboolean in_user_session;

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

    if (WIFEXITED (status))
        g_debug ("Session exited with return value %d", WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        g_debug ("Session terminated with signal %d", WTERMSIG (status));

    display->priv->session_pid = 0;

    // FIXME: Check for respawn loops
    if (display->priv->authenticated && !display->priv->in_user_session)
        start_user_session (display);
    //else
    //    start_greeter (display);
}

static void
start_session (Display *display, const char *username, char * const argv[])
{
    pid_t pid;
    struct passwd *user_info;

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

    pid = fork ();
    if (pid < 0)
    {
        g_warning ("Failed to fork session: %s", strerror (errno));
        return;
    }
    else if (pid > 0)
    {
        g_debug ("Child process started with PID %d", pid);
        display->priv->session_pid = pid;
        g_child_watch_add (display->priv->session_pid, session_watch_cb, display);
        return;
    }

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

    /* Reset environment */
    // FIXME: Check these work
    clearenv ();
    setenv ("USER", user_info->pw_name, 1);
    setenv ("HOME", user_info->pw_dir, 1);
    setenv ("SHELL", user_info->pw_shell, 1);
    setenv ("HOME", user_info->pw_dir, 1);
    setenv ("DISPLAY", ":0", 1);

    execv (argv[0], argv);
}

static void
start_user_session (Display *display)
{ 
    char *argv[] = { display->priv->user_session, NULL };

    g_debug ("Launching session %s for user %s", display->priv->user_session, display->priv->username);

    display->priv->in_user_session = TRUE;
    start_session (display, display->priv->username, argv);
}

static void
start_greeter (Display *display)
{
    char *argv[] = { GREETER_BINARY, NULL };

    g_debug ("Launching greeter %s as user %s", GREETER_BINARY, GREETER_USER);

    display->priv->in_user_session = FALSE;
    g_free (display->priv->username);
    display->priv->username = NULL;
    start_session (display, GREETER_USER, argv);
}

#define DBUS_STRUCT_INT_STRING dbus_g_type_get_struct ("GValueArray", G_TYPE_INT, G_TYPE_STRING, G_TYPE_INVALID)

static void
pam_messages_cb (PAMAuthenticator *authenticator, int num_msg, const struct pam_message **msg, Display *display)
{
    GPtrArray *request;
    int i;
    DBusGMethodInvocation *context;

    /* Respond to d-bus query with messages */
    request = g_ptr_array_new ();
    for (i = 0; i < num_msg; i++)
    {
        GValue value = { 0 };
      
        g_value_init (&value, DBUS_STRUCT_INT_STRING);
        g_value_take_boxed (&value, dbus_g_type_specialized_construct (DBUS_STRUCT_INT_STRING));
        // FIXME: Need to convert to UTF-8
        dbus_g_type_struct_set (&value, 0, msg[i]->msg_style, 1, msg[i]->msg, G_MAXUINT);
        g_ptr_array_add (request, g_value_get_boxed (&value));
    }

    context = display->priv->dbus_context;
    display->priv->dbus_context = NULL;

    dbus_g_method_return (context, 0, request);
}

static void
authenticate_cb (PAMAuthenticator *authenticator, int result, Display *display)
{
    GPtrArray *request;
    DBusGMethodInvocation *context;

    display->priv->authenticated = (result == PAM_SUCCESS);

    /* Respond to D-Bus request */
    request = g_ptr_array_new ();
    context = display->priv->dbus_context;
    display->priv->dbus_context = NULL;

    dbus_g_method_return (context, result, request);
}

gboolean
display_start_authentication (Display *display, const char *username, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    // FIXME: Only allow calls from the correct greeter

    /* Store authentication request and D-Bus request to respond to */
    g_free (display->priv->username);
    display->priv->username = g_strdup (username);
    display->priv->dbus_context = context;

    display->priv->authenticator = pam_authenticator_new ();
    g_signal_connect (G_OBJECT (display->priv->authenticator), "got-messages", G_CALLBACK (pam_messages_cb), display);
    g_signal_connect (G_OBJECT (display->priv->authenticator), "authentication-complete", G_CALLBACK (authenticate_cb), display);
    if (!pam_authenticator_start (display->priv->authenticator, display->priv->username, &error))
    {
        g_warning ("Failed to start authentication: %s", error->message);
        display->priv->dbus_context = NULL; // FIXME: D-Bus return error
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

    g_return_val_if_fail (display->priv->authenticator != NULL, FALSE);
    g_return_val_if_fail (display->priv->dbus_context == NULL, FALSE);

    // FIXME: Only allow calls from the correct greeter

    num_messages = pam_authenticator_get_num_messages (display->priv->authenticator);
    messages = pam_authenticator_get_messages (display->priv->authenticator);

    /* Check correct number of responses */
    for (i = 0; i < num_messages; i++)
    {
        int msg_style = messages[i]->msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
            n_secrets++;
    }
    if (g_strv_length (secrets) != n_secrets)
    {
        pam_authenticator_cancel (display->priv->authenticator);
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
            response[i].resp = g_strdup (secrets[j]); // FIXME: Need to convert from UTF-8
            j++;
        }
    }

    display->priv->dbus_context = context;  
    pam_authenticator_respond (display->priv->authenticator, response);

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

static void
display_init (Display *display)
{
    GError *error = NULL;
    char *argv[] = { "/usr/bin/X", ":0", NULL };
    char *env[] = { NULL };
    gboolean result;

    display->priv = G_TYPE_INSTANCE_GET_PRIVATE (display, DISPLAY_TYPE, DisplayPrivate);

    display->priv->user_session = g_strdup ("/usr/bin/gnome-terminal");

    result = g_spawn_async (NULL, /* Working directory */
                            argv,
                            env,
                            G_SPAWN_DO_NOT_REAP_CHILD,
                            NULL, NULL,
                            &display->priv->xserver_pid,
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
