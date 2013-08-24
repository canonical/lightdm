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
    /* Session type */
    gchar *session_type;

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

    /* PAM service to use */
    gchar *pam_service;

    /* TRUE if should run PAM authentication phase */
    gboolean do_authenticate;

    /* TRUE if can handle PAM prompts */
    gboolean is_interactive;

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
    XAuthority *x_authority;
    gboolean x_authority_use_system_location;

    /* Remote host this session is being controlled from */
    gchar *remote_host_name;

    /* Console kit cookie */
    gchar *console_kit_cookie;

    /* login1 session */
    gchar *login1_session;

    /* Environment to set in child */
    GList *env;

    /* Command to run in child */
    gchar **argv;

    /* True if have run command */
    gboolean command_run;

    /* TRUE if stopping this session */
    gboolean stopping;
};

/* Maximum length of a string to pass between daemon and session */
#define MAX_STRING_LENGTH 65535

static void session_logger_iface_init (LoggerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (Session, session, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (
                             LOGGER_TYPE, session_logger_iface_init));

Session *
session_new (void)
{
    return g_object_new (SESSION_TYPE, NULL);
}

void
session_set_session_type (Session *session, const gchar *session_type)
{
    g_return_if_fail (session != NULL);
    g_free (session->priv->session_type);
    session->priv->session_type = g_strdup (session_type);
}

const gchar *
session_get_session_type (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->session_type;
}

void
session_set_pam_service (Session *session, const gchar *pam_service)
{
    g_return_if_fail (session != NULL);
    g_free (session->priv->pam_service);
    session->priv->pam_service = g_strdup (pam_service);
}

void
session_set_username (Session *session, const gchar *username)
{
    g_return_if_fail (session != NULL);
    g_free (session->priv->username);
    session->priv->username = g_strdup (username);
}

void
session_set_do_authenticate (Session *session, gboolean do_authenticate)
{
    g_return_if_fail (session != NULL);
    session->priv->do_authenticate = do_authenticate;
}

void
session_set_is_interactive (Session *session, gboolean is_interactive)
{
    g_return_if_fail (session != NULL);
    session->priv->is_interactive = is_interactive;
}

void
session_set_is_guest (Session *session, gboolean is_guest)
{
    g_return_if_fail (session != NULL);
    session->priv->is_guest = is_guest;
}

gboolean
session_get_is_guest (Session *session)
{
    g_return_val_if_fail (session != NULL, FALSE);
    return session->priv->is_guest;
}

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

void
session_set_display_server (Session *session, DisplayServer *display_server)
{
    g_return_if_fail (session != NULL);
    g_return_if_fail (display_server != NULL);
    if (session->priv->display_server)
    {
        display_server_disconnect_session (session->priv->display_server, session);
        g_object_unref (session->priv->display_server);
    }
    session->priv->display_server = g_object_ref (display_server);
}

DisplayServer *
session_get_display_server (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->display_server;
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
session_set_x_authority (Session *session, XAuthority *authority, gboolean use_system_location)
{
    g_return_if_fail (session != NULL);
    if (session->priv->x_authority)
    {
        g_object_unref (session->priv->x_authority);
        session->priv->x_authority = NULL;
    }
    if (authority)
        session->priv->x_authority = g_object_ref (authority);
    session->priv->x_authority_use_system_location = use_system_location;
}

void
session_set_remote_host_name (Session *session, const gchar *remote_host_name)
{
    g_return_if_fail (session != NULL);
    g_free (session->priv->remote_host_name);
    session->priv->remote_host_name = g_strdup (remote_host_name);
}

static GList *
find_env_entry (Session *session, const gchar *name)
{
    GList *link;

    for (link = session->priv->env; link; link = link->next)
    {
        const gchar *entry = link->data;

        if (g_str_has_prefix (entry, name) && entry[strlen (name)] == '=')
            return link;
    }

    return NULL;
}

void
session_set_env (Session *session, const gchar *name, const gchar *value)
{
    GList *link;
    gchar *entry;

    g_return_if_fail (session != NULL);
    g_return_if_fail (value != NULL);

    entry = g_strdup_printf ("%s=%s", name, value);

    link = find_env_entry (session, name);
    if (link)
    {
        g_free (link->data);
        link->data = entry;
    }
    else
        session->priv->env = g_list_append (session->priv->env, entry);
}

void
session_unset_env (Session *session, const gchar *name)
{
    GList *link;

    g_return_if_fail (session != NULL);
  
    link = find_env_entry (session, name);
    if (!link)
        return;

    g_free (link->data);
    session->priv->env = g_list_remove_link (session->priv->env, link);
}

void
session_set_argv (Session *session, gchar **argv)
{
    g_return_if_fail (session != NULL);
    session->priv->argv = g_strdupv (argv);
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
        l_warning (session, "Error writing to session: %s", strerror (errno));
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
write_xauth (Session *session, XAuthority *x_authority)
{
    guint16 family;
    gsize length;

    if (!x_authority)
    {
        write_string (session, NULL);
        return;
    }

    write_string (session, x_authority_get_authorization_name (session->priv->x_authority));
    family = x_authority_get_family (session->priv->x_authority);
    write_data (session, &family, sizeof (family));
    length = x_authority_get_address_length (session->priv->x_authority);
    write_data (session, &length, sizeof (length));
    write_data (session, x_authority_get_address (session->priv->x_authority), length);
    write_string (session, x_authority_get_number (session->priv->x_authority));
    length = x_authority_get_authorization_data_length (session->priv->x_authority);
    write_data (session, &length, sizeof (length));
    write_data (session, x_authority_get_authorization_data (session->priv->x_authority), length);
}

static ssize_t
read_from_child (Session *session, void *buf, size_t count)
{
    ssize_t n_read;
    n_read = read (session->priv->from_child_output, buf, count);
    if (n_read < 0)
        l_warning (session, "Error reading from session: %s", strerror (errno));
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
        l_warning (session, "Invalid string length %d from child", length);
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

    if (WIFEXITED (status))
        l_debug (session, "Exited with return value %d", WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        l_debug (session, "Terminated with signal %d", WTERMSIG (status));

    /* do this as late as possible for log messages prefix */
    session->priv->pid = 0;

    /* If failed during authentication then report this as an authentication failure */
    if (session->priv->authentication_started && !session->priv->authentication_complete)
    {
        l_debug (session, "Failed during authentication");
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
        l_debug (session, "Error reading from child: %s", strerror (errno));
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

        l_debug (session, "Authentication complete with return value %d: %s", session->priv->authentication_result, session->priv->authentication_result_string);

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

        l_debug (session, "Got %d message(s) from PAM", session->priv->messages_length);

        g_signal_emit (G_OBJECT (session), signals[GOT_MESSAGES], 0);
    }

    return TRUE;
}

gboolean
session_start (Session *session)
{
    g_return_val_if_fail (session != NULL, FALSE);
    return SESSION_GET_CLASS (session)->start (session);
}

gboolean
session_get_is_started (Session *session)
{
    return session->priv->pid != 0;
}

static gboolean
session_real_start (Session *session)
{
    int version;
    int to_child_pipe[2], from_child_pipe[2];
    int to_child_output, from_child_input;

    g_return_val_if_fail (session->priv->pid == 0, FALSE);

    if (session->priv->display_server)
        display_server_connect_session (session->priv->display_server, session);

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
    if (session->priv->is_guest && session->priv->username == NULL)
    {
        session->priv->username = guest_account_setup ();
        if (!session->priv->username)
            return FALSE;
    }

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
    write_string (session, session->priv->pam_service);
    write_string (session, session->priv->username);
    write_data (session, &session->priv->do_authenticate, sizeof (session->priv->do_authenticate));
    write_data (session, &session->priv->is_interactive, sizeof (session->priv->is_interactive));
    write_string (session, session->priv->class);
    write_string (session, session->priv->tty);
    write_string (session, session->priv->remote_host_name);
    write_string (session, session->priv->xdisplay);
    write_xauth (session, session->priv->x_authority);

    l_debug (session, "Started with service '%s', username '%s'", session->priv->pam_service, session->priv->username);

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
session_run (Session *session)
{
    g_return_if_fail (session->priv->display_server != NULL);
    return SESSION_GET_CLASS (session)->run (session);
}

static void
session_real_run (Session *session)
{
    gsize i, argc;
    gchar *command, *x_authority_filename;
    GList *link;

    g_return_if_fail (session != NULL);
    g_return_if_fail (!session->priv->command_run);
    g_return_if_fail (session_get_is_authenticated (session));
    g_return_if_fail (session->priv->argv != NULL);
    g_return_if_fail (session->priv->pid != 0);

    display_server_connect_session (session->priv->display_server, session);

    session->priv->command_run = TRUE;

    command = g_strjoinv (" ", session->priv->argv);
    l_debug (session, "Running command %s", command);
    g_free (command);

    /* Create authority location */
    if (session->priv->x_authority_use_system_location)
    {
        gchar *run_dir, *dir;

        run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
        dir = g_build_filename (run_dir, session->priv->username, NULL);
        g_free (run_dir);

        if (g_mkdir_with_parents (dir, S_IRWXU) < 0)
            l_warning (session, "Failed to set create system authority dir %s: %s", dir, strerror (errno));
        if (getuid () == 0)
        {
            if (chown (dir, user_get_uid (session_get_user (session)), user_get_gid (session_get_user (session))) < 0)
                l_warning (session, "Failed to set ownership of user authority dir: %s", strerror (errno));
        }

        x_authority_filename = g_build_filename (dir, "xauthority", NULL);
        g_free (dir);
    }
    else
        x_authority_filename = g_build_filename (user_get_home_directory (session_get_user (session)), ".Xauthority", NULL);

    write_string (session, session->priv->log_filename);
    write_string (session, session->priv->tty);
    write_string (session, x_authority_filename);
    g_free (x_authority_filename);
    write_string (session, session->priv->xdisplay);
    write_xauth (session, session->priv->x_authority);
    argc = g_list_length (session->priv->env);
    write_data (session, &argc, sizeof (argc));
    for (link = session->priv->env; link; link = link->next)
        write_string (session, (gchar *) link->data);
    argc = g_strv_length (session->priv->argv);
    write_data (session, &argc, sizeof (argc));
    for (i = 0; i < argc; i++)
        write_string (session, session->priv->argv[i]);

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
        if (session->priv->login1_session)
            login1_lock_session (session->priv->login1_session);
        else if (session->priv->console_kit_cookie)
            ck_lock_session (session->priv->console_kit_cookie);
    }
}

void
session_unlock (Session *session)
{
    g_return_if_fail (session != NULL);
    if (getuid () == 0)
    {
        if (session->priv->login1_session)
            login1_unlock_session (session->priv->login1_session);
        else if (session->priv->console_kit_cookie)
            ck_unlock_session (session->priv->console_kit_cookie);
    }
}

void
session_stop (Session *session)
{
    g_return_if_fail (session != NULL);

    if (session->priv->stopping)
        return;
    session->priv->stopping = TRUE;

    return SESSION_GET_CLASS (session)->stop (session);
}

static void
session_real_stop (Session *session)
{
    g_return_if_fail (session != NULL);

    if (session->priv->pid > 0)
    {
        l_debug (session, "Sending SIGTERM");
        kill (session->priv->pid, SIGTERM);
        // FIXME: Handle timeout
    }
    else
        g_signal_emit (G_OBJECT (session), signals[STOPPED], 0);
}

gboolean
session_get_is_stopping (Session *session)
{
    g_return_val_if_fail (session != NULL, FALSE);
    return session->priv->stopping;
}

static void
session_init (Session *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, SESSION_TYPE, SessionPrivate);
    session->priv->log_filename = g_strdup (".xsession-errors");
}

static void
session_finalize (GObject *object)
{
    Session *self = SESSION (object);
    int i;

    g_free (self->priv->session_type);
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
    g_free (self->priv->pam_service);
    for (i = 0; i < self->priv->messages_length; i++)
        g_free ((char *) self->priv->messages[i].msg);
    g_free (self->priv->messages);
    g_free (self->priv->authentication_result_string);
    g_free (self->priv->log_filename);
    g_free (self->priv->class);
    g_free (self->priv->tty);
    g_free (self->priv->xdisplay);
    if (self->priv->x_authority)
        g_object_unref (self->priv->x_authority);
    g_free (self->priv->remote_host_name);
    g_free (self->priv->login1_session);
    g_free (self->priv->console_kit_cookie);
    g_list_free_full (self->priv->env, g_free);
    g_strfreev (self->priv->argv);

    G_OBJECT_CLASS (session_parent_class)->finalize (object);
}

static void
session_class_init (SessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->start = session_real_start;
    klass->run = session_real_run;
    klass->stop = session_real_stop;
    object_class->finalize = session_finalize;

    g_type_class_add_private (klass, sizeof (SessionPrivate));

    signals[GOT_MESSAGES] =
        g_signal_new ("got-messages",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, got_messages),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[AUTHENTICATION_COMPLETE] =
        g_signal_new ("authentication-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, authentication_complete),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, stopped),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static gint
session_real_logprefix (Logger *self, gchar *buf, gulong buflen)
{
    Session *session = SESSION (self);
    if (session->priv->pid != 0)
        return g_snprintf (buf, buflen, "Session pid=%d: ", session->priv->pid);
    else
        return g_snprintf (buf, buflen, "Session: ");
}

static void
session_logger_iface_init (LoggerInterface *iface)
{
    iface->logprefix = &session_real_logprefix;
}
