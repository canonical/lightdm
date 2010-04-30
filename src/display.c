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
#include <security/pam_appl.h>

#include "display.h"
#include "display-glue.h"

enum {
    EXITED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct DisplayPrivate
{
    /* X process */
    GPid xserver_pid;

    /* Session process (either greeter or user session) */
    GPid session_pid;

    /* Authentication thread */
    GThread *authentication_thread;

    /* Queue to feed responses to the authentication thread */
    GAsyncQueue *authentication_response_queue;
  
    /* Current D-Bus call context */
    DBusGMethodInvocation *dbus_context;

    /* Authentication handle */
    pam_handle_t *pam_handle;

    /* User logged in as */
    char *username;

    /* TRUE if user has been authenticated */
    gboolean authenticated;
  
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
    char *argv[] = { "/usr/bin/gnome-terminal", NULL };
    display->priv->in_user_session = TRUE;
    start_session (display, display->priv->username, argv);
}

static void
start_greeter (Display *display)
{
    char *argv[] = { "/usr/bin/lightdm-greeter", NULL };
    display->priv->in_user_session = FALSE;
    g_free (display->priv->username);
    display->priv->username = NULL;
    start_session (display, "gdm", argv);
}

// FIXME: Make a PAM GObject

#define DBUS_STRUCT_INT_STRING dbus_g_type_get_struct ("GValueArray", G_TYPE_INT, G_TYPE_STRING, G_TYPE_INVALID)

static int
pam_conv_cb (int num_msg, const struct pam_message **msg,
             struct pam_response **resp, void *app_data)
{
    Display *display = app_data;
    GPtrArray *request;
    gchar **secrets;
    int i, secret_index = 0, n_secrets = 0;

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

        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
            n_secrets++;
    }
    dbus_g_method_return (display->priv->dbus_context, 0, request);
    display->priv->dbus_context = NULL;

    /* Wait for secrets */
    secrets = g_async_queue_pop (display->priv->authentication_response_queue);
    if (g_strv_length (secrets) != n_secrets)
        return PAM_SYSTEM_ERR;

    *resp = calloc (num_msg, sizeof (struct pam_response));
    if (*resp == NULL)
        return PAM_BUF_ERR;
  
    /* Fill responses */
    for (i = 0; i < num_msg; i++)
    {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
        {
            // FIXME: Need to convert from UTF-8
            (*resp)[i].resp = g_strdup (secrets[secret_index]);
            secret_index++;
        }
    }
  
    g_strfreev (secrets);

    return 0;
}

static gpointer
authenticate_cb (gpointer data)
{
    Display *display = data;
    struct pam_conv conversation = { pam_conv_cb, display };
    int result;
    GPtrArray *request;

    pam_start ("check_pass", display->priv->username, &conversation, &display->priv->pam_handle);
    result = pam_authenticate (display->priv->pam_handle, 0);
    g_debug ("pam_authenticate -> %s", pam_strerror (display->priv->pam_handle, result));

    /* Thread complete */
    display->priv->authentication_thread = NULL;
    g_async_queue_unref (display->priv->authentication_response_queue);
    display->priv->authentication_response_queue = NULL;

    /* Respond to D-Bus request */
    request = g_ptr_array_new ();
    dbus_g_method_return (display->priv->dbus_context, result, request);
    display->priv->dbus_context = NULL;

    return NULL;
}

gboolean
display_start_authentication (Display *display, const char *username, DBusGMethodInvocation *context)
{
    GError *error = NULL;

    // FIXME: Only allow calls from the greeter

    g_return_val_if_fail (display->priv->authentication_thread == NULL, FALSE);

    /* Store authentication request and D-Bus request to respond to */
    g_free (display->priv->username);
    display->priv->username = g_strdup (username);
    display->priv->dbus_context = context;

    /* Start thread */
    // FIXME: Need to be able to abort the thread on error
    display->priv->authentication_response_queue = g_async_queue_new ();
    display->priv->authentication_thread = g_thread_create (authenticate_cb, display, FALSE, &error);
    if (!display->priv->authentication_thread)
    {
        g_warning ("Failed to start authentication thread: %s", error->message);
        display->priv->dbus_context = NULL;
        return FALSE;
    }
    g_clear_error (&error);

    return TRUE;
}

gboolean
display_continue_authentication (Display *display, gchar **secrets, DBusGMethodInvocation *context)
{
    g_return_val_if_fail (display->priv->authentication_thread != NULL, FALSE);
    g_return_val_if_fail (display->priv->dbus_context == NULL, FALSE);

    // FIXME: Only allow calls from the greeter

    /* Push onto queue and store request to respond to */
    display->priv->dbus_context = context;
    g_async_queue_push (display->priv->authentication_response_queue, g_strdupv (secrets));

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
