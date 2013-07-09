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
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <grp.h>
#include <pwd.h>

#include "session.h"
#include "configuration.h"
#include "console-kit.h"
#include "login1.h"
#include "guest-account.h"

enum {
    GOT_MESSAGES,
    AUTHENTICATION_COMPLETE,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct SessionPrivate
{
    /* Display server running on */
    DisplayServer *display_server;

    /* PID of child process */
    GPid pid;

    /* Pipes to talk to child */
    int to_child_input;
    int from_child_output;
    GIOChannel *from_child_channel;
    guint from_child_watch;
    guint child_watch;

    /* User to authenticate as */
    gchar *username;

    /* TRUE if is a guest account */
    gboolean is_guest;

    /* User object that matches the current username */
    User *user;

    /* Messages being requested by PAM */
    int messages_length;
    struct pam_message *messages;

    /* Authentication result from PAM */
    gboolean authentication_started;
    gboolean authentication_complete;
    int authentication_result;
    gchar *authentication_result_string;

    /* File to log to */
    gchar *log_filename;

    /* Seat class */
    gchar *class;

    /* tty this session is running on */
    gchar *tty;

    /* X display connected to */
    gchar *xdisplay;
    XAuthority *xauthority;
    gboolean xauth_use_system_location;

    /* Remote host this session is being controlled from */
    gchar *remote_host_name;

    /* Console kit cookie */
    gchar *console_kit_cookie;

    /* login1 session */
    gchar *login1_session;

    /* Environment to set in child */
    GList *env;
};

/* Maximum length of a string to pass between daemon and session */
#define MAX_STRING_LENGTH 65535

G_DEFINE_TYPE (Session, session, G_TYPE_OBJECT);

void
session_set_log_file (Session *session, const gchar *filename)
{
    g_return_if_fail (session != NULL);
    g_free (session->priv->log_filename);
    session->priv->log_filename = g_strdup (filename);
}

void
session_set_class (Session *session, const gchar *class)
{
    g_return_if_fail (session != NULL);
    g_free (session->priv->class);
    session->priv->class = g_strdup (class);
}

static void
session_real_set_display_server (Session *session, DisplayServer *display_server)
{
    if (session->priv->display_server)
        g_object_unref (session->priv->display_server);
    session->priv->display_server = g_object_ref (display_server);
}

void
session_set_display_server (Session *session, DisplayServer *display_server)
{
    g_return_if_fail (session != NULL);
    g_return_if_fail (display_server != NULL);
    SESSION_GET_CLASS (session)->set_display_server (session, display_server);
}

void
session_set_tty (Session *session, const gchar *tty)
{
    g_return_if_fail (session != NULL);
    g_free (session->priv->tty);
    session->priv->tty = g_strdup (tty);
}

void
session_set_xdisplay (Session *session, const gchar *xdisplay)
{
    g_return_if_fail (session != NULL);
    g_free (session->priv->xdisplay);
    session->priv->xdisplay = g_strdup (xdisplay);
}

void
session_set_xauthority (Session *session, XAuthority *authority, gboolean use_system_location)
{
    g_return_if_fail (session != NULL);
    if (session->priv->xauthority)
        g_object_unref (session->priv->xauthority);
    session->priv->xauthority = g_object_ref (authority);
    session->priv->xauth_use_system_location = use_system_location;
}

void
session_set_remote_host_name (Session *session, const gchar *remote_host_name)
{
    g_return_if_fail (session != NULL);
    g_free (session->priv->remote_host_name);
    session->priv->remote_host_name = g_strdup (remote_host_name);
}

void
session_set_env (Session *session, const gchar *name, const gchar *value)
{
    g_return_if_fail (session != NULL);
    session->priv->env = g_list_append (session->priv->env, g_strdup_printf ("%s=%s", name, value));
}

User *
session_get_user (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);

    if (session->priv->username == NULL)
        return NULL;

    if (!session->priv->user)
        session->priv->user = accounts_get_user_by_name (session->priv->username);

    return session->priv->user;
}

static void
write_data (Session *session, const void *buf, size_t count)
{
    if (write (session->priv->to_child_input, buf, count) != count)
        g_warning ("Error writing to session: %s", strerror (errno));
}

static void
write_string (Session *session, const char *value)
{
    int length;

    length = value ? strlen (value) : -1;
    write_data (session, &length, sizeof (length));
    if (value)
        write_data (session, value, sizeof (char) * length);
}

static void
write_xauth (Session *session, XAuthority *xauthority)
{
    guint16 family;
    gsize length;

    if (!xauthority)
    {
        write_string (session, NULL);
        return;
    }

    write_string (session, xauth_get_authorization_name (session->priv->xauthority));
    family = xauth_get_family (session->priv->xauthority);
    write_data (session, &family, sizeof (family));
    length = xauth_get_address_length (session->priv->xauthority);
    write_data (session, &length, sizeof (length));
    write_data (session, xauth_get_address (session->priv->xauthority), length);
    write_string (session, xauth_get_number (session->priv->xauthority));
    length = xauth_get_authorization_data_length (session->priv->xauthority);
    write_data (session, &length, sizeof (length));
    write_data (session, xauth_get_authorization_data (session->priv->xauthority), length);
}

static ssize_t
read_from_child (Session *session, void *buf, size_t count)
{
    ssize_t n_read;
    n_read = read (session->priv->from_child_output, buf, count);
    if (n_read < 0)
        g_warning ("Error reading from session: %s", strerror (errno));
    return n_read;
}

static gchar *
read_string_from_child (Session *session)
{
    int length;
    char *value;

    if (read_from_child (session, &length, sizeof (length)) <= 0)
        return NULL;
    if (length < 0)
        return NULL;
    if (length > MAX_STRING_LENGTH)
    {
        g_warning ("Invalid string length %d from child", length);
        return NULL;
    }

    value = g_malloc (sizeof (char) * (length + 1));
    read_from_child (session, value, length);
    value[length] = '\0';

    return value;
}

static void
session_watch_cb (GPid pid, gint status, gpointer data)
{
    Session *session = data;

    session->priv->pid = 0;

    if (WIFEXITED (status))
        g_debug ("Session %d exited with return value %d", pid, WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        g_debug ("Session %d terminated with signal %d", pid, WTERMSIG (status));

    /* If failed during authentication then report this as an authentication failure */
    if (session->priv->authentication_started && !session->priv->authentication_complete)
    {
        g_debug ("Session %d failed during authentication", pid);
        session->priv->authentication_complete = TRUE;
        session->priv->authentication_result = PAM_CONV_ERR;
        g_free (session->priv->authentication_result_string);
        session->priv->authentication_result_string = g_strdup ("Authentication stopped before completion");
        g_signal_emit (G_OBJECT (session), signals[AUTHENTICATION_COMPLETE], 0);
    }

    g_signal_emit (G_OBJECT (session), signals[STOPPED], 0);

    /* Delete account if it is a guest one */
    if (session->priv->is_guest)
        guest_account_cleanup (session->priv->username);

    /* Drop our reference on the child process, it has terminated */
    g_object_unref (session);
}

static gboolean
from_child_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    Session *session = data;
    gchar *username;
    ssize_t n_read;
    gboolean auth_complete;

    /* Remote end gone */
    if (condition == G_IO_HUP)
    {
        session->priv->from_child_watch = 0;
        return FALSE;
    }

    /* Get the username currently being authenticated (may change during authentication) */
    username = read_string_from_child (session);
    if (g_strcmp0 (username, session->priv->username) != 0)
    {
        g_free (session->priv->username);
        session->priv->username = username;
        if (session->priv->user)
            g_object_unref (session->priv->user);
        session->priv->user = NULL;
    }
    else
        g_free (username);

    /* Check if authentication completed */
    n_read = read_from_child (session, &auth_complete, sizeof (auth_complete));
    if (n_read < 0)
        g_debug ("Error reading from child: %s", strerror (errno));
    if (n_read <= 0)
    {
        session->priv->from_child_watch = 0;
        return FALSE;
    }

    if (auth_complete)
    {
        session->priv->authentication_complete = TRUE;
        read_from_child (session, &session->priv->authentication_result, sizeof (session->priv->authentication_result));
        g_free (session->priv->authentication_result_string);
        session->priv->authentication_result_string = read_string_from_child (session);

        g_debug ("Session %d authentication complete with return value %d: %s", session->priv->pid, session->priv->authentication_result, session->priv->authentication_result_string);

        /* No longer expect any more messages */
        session->priv->from_child_watch = 0;

        g_signal_emit (G_OBJECT (session), signals[AUTHENTICATION_COMPLETE], 0);

        return FALSE;
    }
    else
    {
        int i;

        session->priv->messages_length = 0;
        read_from_child (session, &session->priv->messages_length, sizeof (session->priv->messages_length));
        session->priv->messages = calloc (session->priv->messages_length, sizeof (struct pam_message));
        for (i = 0; i < session->priv->messages_length; i++)
        {
            struct pam_message *m = &session->priv->messages[i];
            read_from_child (session, &m->msg_style, sizeof (m->msg_style));
            m->msg = read_string_from_child (session);
        }

        g_debug ("Session %d got %d message(s) from PAM", session->priv->pid, session->priv->messages_length);

        g_signal_emit (G_OBJECT (session), signals[GOT_MESSAGES], 0);
    }

    return TRUE;
}

gboolean
session_start (Session *session, const gchar *service, const gchar *username, gboolean do_authenticate, gboolean is_interactive, gboolean is_guest)
{
    int version;
    int to_child_pipe[2], from_child_pipe[2];
    int to_child_output, from_child_input;

    g_return_val_if_fail (session != NULL, FALSE);
    g_return_val_if_fail (service != NULL, FALSE);
    g_return_val_if_fail (session->priv->pid == 0, FALSE);

    /* Create pipes to talk to the child */
    if (pipe (to_child_pipe) < 0 || pipe (from_child_pipe) < 0)
    {
        g_warning ("Failed to create pipe to communicate with session process: %s", strerror (errno));
        return FALSE;
    }
    to_child_output = to_child_pipe[0];
    session->priv->to_child_input = to_child_pipe[1];
    session->priv->from_child_output = from_child_pipe[0];
    from_child_input = from_child_pipe[1];
    session->priv->from_child_channel = g_io_channel_unix_new (session->priv->from_child_output);
    session->priv->from_child_watch = g_io_add_watch (session->priv->from_child_channel, G_IO_IN | G_IO_HUP, from_child_cb, session);

    /* Don't allow the daemon end of the pipes to be accessed in child processes */
    fcntl (session->priv->to_child_input, F_SETFD, FD_CLOEXEC);
    fcntl (session->priv->from_child_output, F_SETFD, FD_CLOEXEC);

    /* Create the guest account if it is one */
    session->priv->is_guest = is_guest;
    if (is_guest && username == NULL)
        username = guest_account_setup ();

    /* Remember what username we started with - it will be updated by PAM during authentication */
    session->priv->username = g_strdup (username);

    /* Run the child */
    session->priv->pid = fork ();
    if (session->priv->pid < 0)
    {
        g_debug ("Failed to fork session child process: %s", strerror (errno));
        return FALSE;
    }

    if (session->priv->pid == 0)
    {
        /* Run us again in session child mode */
        execlp ("lightdm",
                "lightdm",
                "--session-child",
                g_strdup_printf ("%d", to_child_output),
                g_strdup_printf ("%d", from_child_input),
                NULL);
        _exit (EXIT_FAILURE);
    }

    /* Hold a reference on this object until the child process terminates so we
     * can handle the watch callback even if it is no longer used. Otherwise a
     * zombie process will remain */
    g_object_ref (session);

    /* Listen for session termination */
    session->priv->authentication_started = TRUE;
    session->priv->child_watch = g_child_watch_add (session->priv->pid, session_watch_cb, session);

    /* Close the ends of the pipes we don't need */
    close (to_child_output);
    close (from_child_input);

    /* Indicate what version of the protocol we are using */
    version = 1;
    write_data (session, &version, sizeof (version));

    /* Send configuration */
    write_string (session, service);
    write_string (session, username);
    write_data (session, &do_authenticate, sizeof (do_authenticate));
    write_data (session, &is_interactive, sizeof (is_interactive));
    write_string (session, session->priv->class);
    write_string (session, session->priv->tty);
    write_string (session, session->priv->remote_host_name);
    write_string (session, session->priv->xdisplay);
    write_xauth (session, session->priv->xauthority);

    g_debug ("Started session %d with service '%s', username '%s'", session->priv->pid, service, username);

    return TRUE;
}

const gchar *
session_get_username (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->username;
}

const gchar *
session_get_console_kit_cookie (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->console_kit_cookie;
}

void
session_respond (Session *session, struct pam_response *response)
{
    int error = PAM_SUCCESS;
    int i;

    g_return_if_fail (session != NULL);

    write_data (session, &error, sizeof (error));
    for (i = 0; i < session->priv->messages_length; i++)
    {
        write_string (session, response[i].resp);
        write_data (session, &response[i].resp_retcode, sizeof (response[i].resp_retcode));
    }

    /* Delete the old messages */
    for (i = 0; i < session->priv->messages_length; i++)
        g_free ((char *) session->priv->messages[i].msg);
    g_free (session->priv->messages);
    session->priv->messages = NULL;
    session->priv->messages_length = 0;
}

void
session_respond_error (Session *session, int error)
{
    g_return_if_fail (session != NULL);
    g_return_if_fail (error != PAM_SUCCESS);

    write_data (session, &error, sizeof (error));
}

int
session_get_messages_length (Session *session)
{
    g_return_val_if_fail (session != NULL, 0);
    return session->priv->messages_length;
}

const struct pam_message *
session_get_messages (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->messages;
}

gboolean
session_get_is_authenticated (Session *session)
{
    g_return_val_if_fail (session != NULL, FALSE);
    return session->priv->authentication_complete && session->priv->authentication_result == PAM_SUCCESS;
}

int
session_get_authentication_result (Session *session)
{
    g_return_val_if_fail (session != NULL, 0);
    return session->priv->authentication_result;
}

const gchar *
session_get_authentication_result_string (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->authentication_result_string;
}

void
session_run (Session *session, gchar **argv)
{
    gsize i, argc;
    gchar *command, *filename;
    GList *link;

    g_return_if_fail (session != NULL);
    g_return_if_fail (session_get_is_authenticated (session));

    command = g_strjoinv (" ", argv);
    g_debug ("Session %d running command %s", session->priv->pid, command);
    g_free (command);

    /* Create authority location */
    if (session->priv->xauth_use_system_location)
    {
        gchar *run_dir, *dir;

        run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
        dir = g_build_filename (run_dir, session->priv->username, NULL);
        g_free (run_dir);

        if (g_mkdir_with_parents (dir, S_IRWXU) < 0)
            g_warning ("Failed to set create system authority dir %s: %s", dir, strerror (errno));          
        if (getuid () == 0)
        {
            if (chown (dir, user_get_uid (session_get_user (session)), user_get_gid (session_get_user (session))) < 0)
                g_warning ("Failed to set ownership of user authority dir: %s", strerror (errno));
        }

        filename = g_build_filename (dir, "xauthority", NULL);
        g_free (dir);
    }
    else
        filename = g_build_filename (user_get_home_directory (session_get_user (session)), ".Xauthority", NULL);

    write_string (session, session->priv->log_filename);
    write_string (session, filename);
    g_free (filename);
    write_string (session, session->priv->xdisplay);
    write_xauth (session, session->priv->xauthority);
    argc = g_list_length (session->priv->env);
    write_data (session, &argc, sizeof (argc));
    for (link = session->priv->env; link; link = link->next)
        write_string (session, (gchar *) link->data);
    argc = g_strv_length (argv);
    write_data (session, &argc, sizeof (argc));
    for (i = 0; i < argc; i++)
        write_string (session, argv[i]);

    if (login1_is_running ())
      session->priv->login1_session = read_string_from_child (session);
    if (!session->priv->login1_session)
      session->priv->console_kit_cookie = read_string_from_child (session);
}

void
session_lock (Session *session)
{
    g_return_if_fail (session != NULL);
    if (getuid () == 0)
    {
        if (login1_is_running ())
            login1_lock_session (session->priv->login1_session);
        if (!session->priv->login1_session)
            ck_lock_session (session->priv->console_kit_cookie);
    }
}

void
session_unlock (Session *session)
{
    g_return_if_fail (session != NULL);
    if (getuid () == 0)
    {
        if (login1_is_running ())
            login1_unlock_session (session->priv->login1_session);
        if (!session->priv->login1_session)
            ck_unlock_session (session->priv->console_kit_cookie);
    }
}

void
session_stop (Session *session)
{
    g_return_if_fail (session != NULL);

    if (session->priv->pid > 0)
    {
        g_debug ("Session %d: Sending SIGTERM", session->priv->pid);
        kill (session->priv->pid, SIGTERM);
        // FIXME: Handle timeout
    }
}

gboolean
session_get_is_stopped (Session *session)
{
    g_return_val_if_fail (session != NULL, TRUE);
    return session->priv->pid == 0;
}

static void
session_init (Session *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, SESSION_TYPE, SessionPrivate);
}

static void
session_finalize (GObject *object)
{
    Session *self = SESSION (object);
    int i;

    if (self->priv->display_server)
        g_object_unref (self->priv->display_server);
    if (self->priv->pid)
        kill (self->priv->pid, SIGKILL);
    if (self->priv->from_child_channel)
        g_io_channel_unref (self->priv->from_child_channel);
    if (self->priv->from_child_watch)
        g_source_remove (self->priv->from_child_watch);
    if (self->priv->child_watch)
        g_source_remove (self->priv->child_watch);
    g_free (self->priv->username);
    if (self->priv->user)
        g_object_unref (self->priv->user);
    for (i = 0; i < self->priv->messages_length; i++)
        g_free ((char *) self->priv->messages[i].msg);
    g_free (self->priv->messages);
    g_free (self->priv->authentication_result_string);
    g_free (self->priv->log_filename);
    g_free (self->priv->class);
    g_free (self->priv->tty);
    g_free (self->priv->xdisplay);
    if (self->priv->xauthority)
        g_object_unref (self->priv->xauthority);
    g_free (self->priv->remote_host_name);
    g_free (self->priv->login1_session);
    g_free (self->priv->console_kit_cookie);
    g_list_free_full (self->priv->env, g_free);

    G_OBJECT_CLASS (session_parent_class)->finalize (object);
}

static void
session_class_init (SessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->set_display_server = session_real_set_display_server;
    object_class->finalize = session_finalize;

    g_type_class_add_private (klass, sizeof (SessionPrivate));

    signals[GOT_MESSAGES] =
        g_signal_new ("got-messages",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, got_messages),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    signals[AUTHENTICATION_COMPLETE] =
        g_signal_new ("authentication-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, authentication_complete),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
