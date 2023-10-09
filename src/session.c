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
#include "shared-data-manager.h"
#include "greeter-socket.h"

enum {
    CREATE_GREETER,
    GOT_MESSAGES,
    AUTHENTICATION_COMPLETE,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    /* Configuration for this session */
    SessionConfig *config;

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

    /* Home directory of the authenticating user */
    gchar *home_directory;

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
    size_t messages_length;
    struct pam_message *messages;

    /* Authentication result from PAM */
    gboolean authentication_started;
    gboolean authentication_complete;
    int authentication_result;
    gchar *authentication_result_string;

    /* File to log to */
    gchar *log_filename;
    LogMode log_mode;

    /* tty this session is running on */
    gchar *tty;

    /* X display connected to */
    gchar *xdisplay;
    XAuthority *x_authority;
    gboolean x_authority_use_system_location;

    /* Socket to allow greeters to connect to (if allowed) */
    GreeterSocket *greeter_socket;

    /* Remote host this session is being controlled from */
    gchar *remote_host_name;

    /* Console kit cookie */
    gchar *console_kit_cookie;

    /* login1 session ID */
    gchar *login1_session_id;

    /* Environment to set in child */
    GList *env;

    /* Command to run in child */
    gchar **argv;

    /* True if have run command */
    gboolean command_run;

    /* TRUE if stopping this session */
    gboolean stopping;
} SessionPrivate;

/* Maximum length of a string to pass between daemon and session */
#define MAX_STRING_LENGTH 65535

static void session_logger_iface_init (LoggerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (Session, session, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (Session)
                         G_IMPLEMENT_INTERFACE (
                             LOGGER_TYPE, session_logger_iface_init))

Session *
session_new (void)
{
    return g_object_new (SESSION_TYPE, NULL);
}

void
session_set_config (Session *session, SessionConfig *config)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);

    g_clear_object (&priv->config);
    priv->config = g_object_ref (config);
}

SessionConfig *
session_get_config (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->config;
}

const gchar *
session_get_session_type (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return session_config_get_session_type (priv->config);
}

void
session_set_pam_service (Session *session, const gchar *pam_service)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    g_free (priv->pam_service);
    priv->pam_service = g_strdup (pam_service);
}

void
session_set_username (Session *session, const gchar *username)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    g_free (priv->username);
    priv->username = g_strdup (username);
}

void
session_set_do_authenticate (Session *session, gboolean do_authenticate)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    priv->do_authenticate = do_authenticate;
}

void
session_set_is_interactive (Session *session, gboolean is_interactive)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    priv->is_interactive = is_interactive;
}

void
session_set_is_guest (Session *session, gboolean is_guest)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    priv->is_guest = is_guest;
}

gboolean
session_get_is_guest (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, FALSE);
    return priv->is_guest;
}

void
session_set_log_file (Session *session, const gchar *filename, LogMode log_mode)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    g_free (priv->log_filename);
    priv->log_filename = g_strdup (filename);
    priv->log_mode = log_mode;
}

void
session_set_display_server (Session *session, DisplayServer *display_server)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);
    g_return_if_fail (display_server != NULL);

    if (priv->display_server == display_server)
        return;

    if (priv->display_server)
        display_server_disconnect_session (priv->display_server, session);
    g_clear_object (&priv->display_server);
    priv->display_server = g_object_ref (display_server);
}

DisplayServer *
session_get_display_server (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->display_server;
}

void
session_set_tty (Session *session, const gchar *tty)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    g_free (priv->tty);
    priv->tty = g_strdup (tty);
}

void
session_set_xdisplay (Session *session, const gchar *xdisplay)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    g_free (priv->xdisplay);
    priv->xdisplay = g_strdup (xdisplay);
}

void
session_set_x_authority (Session *session, XAuthority *authority, gboolean use_system_location)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    g_clear_object (&priv->x_authority);
    if (authority)
        priv->x_authority = g_object_ref (authority);
    priv->x_authority_use_system_location = use_system_location;
}

void
session_set_remote_host_name (Session *session, const gchar *remote_host_name)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    g_free (priv->remote_host_name);
    priv->remote_host_name = g_strdup (remote_host_name);
}

static GList *
find_env_entry (Session *session, const gchar *name)
{
    SessionPrivate *priv = session_get_instance_private (session);

    for (GList *link = priv->env; link; link = link->next)
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
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);
    g_return_if_fail (value != NULL);

    gchar *entry = g_strdup_printf ("%s=%s", name, value);

    GList *link = find_env_entry (session, name);
    if (link)
    {
        g_free (link->data);
        link->data = entry;
    }
    else
        priv->env = g_list_append (priv->env, entry);
}

const gchar *
session_get_env (Session *session, const gchar *name)
{
    GList *link = find_env_entry (session, name);
    if (!link)
        return NULL;

    gchar *entry = link->data;

    return entry + strlen (name) + 1;
}

void
session_unset_env (Session *session, const gchar *name)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);

    GList *link = find_env_entry (session, name);
    if (!link)
        return;

    g_free (link->data);
    priv->env = g_list_delete_link (priv->env, link);
}

void
session_set_argv (Session *session, gchar **argv)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    priv->argv = g_strdupv (argv);
}

User *
session_get_user (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_val_if_fail (session != NULL, NULL);

    if (priv->username == NULL)
        return NULL;

    if (!priv->user)
        priv->user = accounts_get_user_by_name (priv->username);

    return priv->user;
}

static void
write_data (Session *session, const void *buf, size_t count)
{
    SessionPrivate *priv = session_get_instance_private (session);
    if (write (priv->to_child_input, buf, count) != count)
        l_warning (session, "Error writing to session: %s", strerror (errno));
}

static void
write_string (Session *session, const char *value)
{
    int length = value ? strlen (value) : -1;
    write_data (session, &length, sizeof (length));
    if (value)
        write_data (session, value, sizeof (char) * length);
}

static void
write_xauth (Session *session, XAuthority *x_authority)
{
    SessionPrivate *priv = session_get_instance_private (session);

    if (!x_authority)
    {
        write_string (session, NULL);
        return;
    }

    write_string (session, x_authority_get_authorization_name (priv->x_authority));
    guint16 family = x_authority_get_family (priv->x_authority);
    write_data (session, &family, sizeof (family));
    gsize length = x_authority_get_address_length (priv->x_authority);
    write_data (session, &length, sizeof (length));
    write_data (session, x_authority_get_address (priv->x_authority), length);
    write_string (session, x_authority_get_number (priv->x_authority));
    length = x_authority_get_authorization_data_length (priv->x_authority);
    write_data (session, &length, sizeof (length));
    write_data (session, x_authority_get_authorization_data (priv->x_authority), length);
}

static ssize_t
read_from_child (Session *session, void *buf, size_t count)
{
    SessionPrivate *priv = session_get_instance_private (session);

    ssize_t n_read = read (priv->from_child_output, buf, count);
    if (n_read < 0)
        l_warning (session, "Error reading from session: %s", strerror (errno));
    return n_read;
}

static gchar *
read_string_from_child (Session *session)
{
    int length;
    if (read_from_child (session, &length, sizeof (length)) <= 0)
        return NULL;
    if (length < 0)
        return NULL;
    if (length > MAX_STRING_LENGTH)
    {
        l_warning (session, "Invalid string length %d from child", length);
        return NULL;
    }

    char *value = g_malloc (sizeof (char) * (length + 1));
    read_from_child (session, value, length);
    value[length] = '\0';

    return value;
}

static void
session_watch_cb (GPid pid, gint status, gpointer data)
{
    Session *session = data;
    SessionPrivate *priv = session_get_instance_private (session);

    priv->child_watch = 0;

    if (WIFEXITED (status))
        l_debug (session, "Exited with return value %d", WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        l_debug (session, "Terminated with signal %d", WTERMSIG (status));

    /* do this as late as possible for log messages prefix */
    priv->pid = 0;

    /* If failed during authentication then report this as an authentication failure */
    if (priv->authentication_started && !priv->authentication_complete)
    {
        l_debug (session, "Failed during authentication");
        priv->authentication_complete = TRUE;
        priv->authentication_result = PAM_CONV_ERR;
        g_free (priv->authentication_result_string);
        priv->authentication_result_string = g_strdup ("Authentication stopped before completion");
        g_signal_emit (G_OBJECT (session), signals[AUTHENTICATION_COMPLETE], 0);
    }

    g_signal_emit (G_OBJECT (session), signals[STOPPED], 0);

    /* Delete account if it is a guest one */
    if (priv->is_guest)
        guest_account_cleanup (priv->username);

    /* Drop our reference on the child process, it has terminated */
    g_object_unref (session);
}

static gboolean
from_child_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    Session *session = data;
    SessionPrivate *priv = session_get_instance_private (session);

    /* Remote end gone */
    if (condition == G_IO_HUP)
    {
        priv->from_child_watch = 0;
        return FALSE;
    }

    /* Get the username currently being authenticated (may change during authentication) */
    g_autofree gchar *username = read_string_from_child (session);
    if (g_strcmp0 (username, priv->username) != 0)
    {
        g_free (priv->username);
        priv->username = g_steal_pointer (&username);
        g_clear_object (&priv->user);
    }

    /* Check if authentication completed */
    gboolean auth_complete;
    ssize_t n_read = read_from_child (session, &auth_complete, sizeof (auth_complete));
    if (n_read < 0)
        l_debug (session, "Error reading from child: %s", strerror (errno));
    if (n_read <= 0)
    {
        priv->from_child_watch = 0;
        return FALSE;
    }

    if (auth_complete)
    {
        priv->authentication_complete = TRUE;
        read_from_child (session, &priv->authentication_result, sizeof (priv->authentication_result));
        g_free (priv->authentication_result_string);
        priv->authentication_result_string = read_string_from_child (session);

        l_debug (session, "Authentication complete with return value %d: %s", priv->authentication_result, priv->authentication_result_string);

        /* No longer expect any more messages */
        priv->from_child_watch = 0;

        g_signal_emit (G_OBJECT (session), signals[AUTHENTICATION_COMPLETE], 0);

        return FALSE;
    }
    else
    {
        priv->messages_length = 0;
        read_from_child (session, &priv->messages_length, sizeof (priv->messages_length));
        priv->messages = calloc (priv->messages_length, sizeof (struct pam_message));
        for (int i = 0; i < priv->messages_length; i++)
        {
            struct pam_message *m = &priv->messages[i];
            read_from_child (session, &m->msg_style, sizeof (m->msg_style));
            m->msg = read_string_from_child (session);
        }

        l_debug (session, "Got %zi message(s) from PAM", priv->messages_length);

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
    SessionPrivate *priv = session_get_instance_private (session);
    return priv->pid != 0;
}

static Greeter *
create_greeter_cb (GreeterSocket *socket, Session *session)
{
    Greeter *greeter = NULL;
    g_signal_emit (session, signals[CREATE_GREETER], 0, &greeter);
    return greeter;
}

static gboolean
session_real_start (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_val_if_fail (priv->pid == 0, FALSE);

    if (priv->display_server)
        display_server_connect_session (priv->display_server, session);

    /* Create pipes to talk to the child */
    int to_child_pipe[2], from_child_pipe[2];
    if (pipe (to_child_pipe) < 0 || pipe (from_child_pipe) < 0)
    {
        g_warning ("Failed to create pipe to communicate with session process: %s", strerror (errno));
        return FALSE;
    }
    int to_child_output = to_child_pipe[0];
    priv->to_child_input = to_child_pipe[1];
    priv->from_child_output = from_child_pipe[0];
    int from_child_input = from_child_pipe[1];
    priv->from_child_channel = g_io_channel_unix_new (priv->from_child_output);
    priv->from_child_watch = g_io_add_watch (priv->from_child_channel, G_IO_IN | G_IO_HUP, from_child_cb, session);

    /* Don't allow the daemon end of the pipes to be accessed in child processes */
    fcntl (priv->to_child_input, F_SETFD, FD_CLOEXEC);
    fcntl (priv->from_child_output, F_SETFD, FD_CLOEXEC);

    /* Create the guest account if it is one */
    if (priv->is_guest && priv->username == NULL)
    {
        priv->username = guest_account_setup ();
        if (!priv->username)
            return FALSE;
    }

    /* Run the child */
    g_autofree gchar *arg0 = g_strdup_printf ("%d", to_child_output);
    g_autofree gchar *arg1 = g_strdup_printf ("%d", from_child_input);
    priv->pid = fork ();
    if (priv->pid == 0)
    {
        /* Run us again in session child mode */
        execlp ("lightdm",
                "lightdm",
                "--session-child",
                arg0, arg1, NULL);
        _exit (EXIT_FAILURE);
    }

    if (priv->pid < 0)
    {
        g_debug ("Failed to fork session child process: %s", strerror (errno));
        return FALSE;
    }

    /* Hold a reference on this object until the child process terminates so we
     * can handle the watch callback even if it is no longer used. Otherwise a
     * zombie process will remain */
    g_object_ref (session);

    /* Listen for session termination */
    priv->authentication_started = TRUE;
    priv->child_watch = g_child_watch_add (priv->pid, session_watch_cb, session);

    /* Close the ends of the pipes we don't need */
    close (to_child_output);
    close (from_child_input);

    /* Indicate what version of the protocol we are using */
    int version = 4;
    write_data (session, &version, sizeof (version));

    /* Send configuration */
    write_string (session, priv->pam_service);
    write_string (session, priv->username);
    write_data (session, &priv->do_authenticate, sizeof (priv->do_authenticate));
    write_data (session, &priv->is_interactive, sizeof (priv->is_interactive));
    write_string (session, NULL); /* Used to be class, now we just use the environment variable */
    write_string (session, priv->tty);
    write_string (session, priv->remote_host_name);
    write_string (session, priv->xdisplay);
    write_xauth (session, priv->x_authority);

    l_debug (session, "Started with service '%s', username '%s'", priv->pam_service, priv->username);

    return TRUE;
}

const gchar *
session_get_username (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->username;
}

const gchar *
session_get_home_directory (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->home_directory;
}

const gchar *
session_get_login1_session_id (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->login1_session_id;
}

const gchar *
session_get_console_kit_cookie (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->console_kit_cookie;
}

void
session_respond (Session *session, struct pam_response *response)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);

    int error = PAM_SUCCESS;
    write_data (session, &error, sizeof (error));
    for (size_t i = 0; i < priv->messages_length; i++)
    {
        write_string (session, response[i].resp);
        write_data (session, &response[i].resp_retcode, sizeof (response[i].resp_retcode));
    }

    /* Delete the old messages */
    for (size_t i = 0; i < priv->messages_length; i++)
        g_free ((char *) priv->messages[i].msg);
    g_free (priv->messages);
    priv->messages = NULL;
    priv->messages_length = 0;
}

void
session_respond_error (Session *session, int error)
{
    g_return_if_fail (session != NULL);
    g_return_if_fail (error != PAM_SUCCESS);

    write_data (session, &error, sizeof (error));
}

size_t
session_get_messages_length (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, 0);
    return priv->messages_length;
}

const struct pam_message *
session_get_messages (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->messages;
}

gboolean
session_get_is_authenticated (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, FALSE);
    return priv->authentication_complete && priv->authentication_result == PAM_SUCCESS;
}

int
session_get_authentication_result (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, 0);
    return priv->authentication_result;
}

const gchar *
session_get_authentication_result_string (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->authentication_result_string;
}

void
session_run (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    g_return_if_fail (priv->display_server != NULL);
    return SESSION_GET_CLASS (session)->run (session);
}

gboolean
session_get_is_run (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, FALSE);
    return priv->command_run;
}

static void
session_real_run (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);
    g_return_if_fail (!priv->command_run);
    g_return_if_fail (session_get_is_authenticated (session));
    g_return_if_fail (priv->argv != NULL);
    g_return_if_fail (priv->pid != 0);

    display_server_connect_session (priv->display_server, session);

    priv->command_run = TRUE;

    g_autofree gchar *command = g_strjoinv (" ", priv->argv);
    l_debug (session, "Running command %s", command);

    /* Create authority location */
    g_autofree gchar *x_authority_filename = NULL;
    if (priv->x_authority_use_system_location)
    {
        g_autofree gchar *run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
        g_autofree gchar *dir = g_build_filename (run_dir, priv->username, NULL);

        if (g_mkdir_with_parents (dir, S_IRWXU) < 0)
            l_warning (session, "Failed to set create system authority dir %s: %s", dir, strerror (errno));
        if (getuid () == 0)
        {
            if (chown (dir, user_get_uid (session_get_user (session)), user_get_gid (session_get_user (session))) < 0)
                l_warning (session, "Failed to set ownership of user authority dir: %s", strerror (errno));
        }

        x_authority_filename = g_build_filename (dir, "xauthority", NULL);
    }
    else
        x_authority_filename = g_strdup (".Xauthority");

    /* Make sure shared user directory for this user exists */
    if (!priv->remote_host_name)
    {
        g_autofree gchar *data_dir = shared_data_manager_ensure_user_dir (shared_data_manager_get_instance (), priv->username);
        if (data_dir)
            session_set_env (session, "XDG_GREETER_DATA_DIR", data_dir);
    }

    /* Open socket to allow in-session greeter */
    if (priv->config && session_config_get_allow_greeter (priv->config))
    {
        g_autofree gchar *run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
        g_autofree gchar *dir = g_build_filename (run_dir, priv->username, NULL);

        if (g_mkdir_with_parents (dir, S_IRWXU) < 0)
            l_warning (session, "Failed to create greeter socket dir %s: %s", dir, strerror (errno));
        if (getuid () == 0)
        {
            if (chown (dir, user_get_uid (session_get_user (session)), user_get_gid (session_get_user (session))) < 0)
                l_warning (session, "Failed to set ownership of greeter socket dir: %s", strerror (errno));
        }

        g_autofree gchar *path = g_build_filename (dir, "greeter-socket", NULL);
        priv->greeter_socket = greeter_socket_new (path);
        g_signal_connect (priv->greeter_socket, GREETER_SOCKET_SIGNAL_CREATE_GREETER, G_CALLBACK (create_greeter_cb), session);
        session_set_env (session, "LIGHTDM_GREETER_PIPE", path);

        g_autoptr(GError) error = NULL;
        if (!greeter_socket_start (priv->greeter_socket, &error))
            l_warning (session, "Failed to start greeter socket: %s\n", error->message);
    }

    if (priv->log_filename)
        l_debug (session, "Logging to %s", priv->log_filename);
    write_string (session, priv->log_filename);
    write_data (session, &priv->log_mode, sizeof (priv->log_mode));
    write_string (session, priv->tty);
    write_string (session, x_authority_filename);
    write_string (session, priv->xdisplay);
    write_xauth (session, priv->x_authority);
    gsize argc = g_list_length (priv->env);
    write_data (session, &argc, sizeof (argc));
    for (GList *link = priv->env; link; link = link->next)
        write_string (session, (gchar *) link->data);
    argc = g_strv_length (priv->argv);
    write_data (session, &argc, sizeof (argc));
    for (gsize i = 0; i < argc; i++)
        write_string (session, priv->argv[i]);

    /* Get the home directory of the user currently being authenticated (may change after opening PAM session) */
    g_autofree gchar *home_directory = read_string_from_child (session);
    if (g_strcmp0 (home_directory, priv->home_directory) != 0)
    {
        g_free (priv->home_directory);
        priv->home_directory = g_steal_pointer (&home_directory);
    }

    priv->login1_session_id = read_string_from_child (session);
    priv->console_kit_cookie = read_string_from_child (session);
}

void
session_lock (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);

    if (getuid () == 0)
    {
        if (priv->login1_session_id)
            login1_service_lock_session (login1_service_get_instance (), priv->login1_session_id);
        else if (priv->console_kit_cookie)
            ck_lock_session (priv->console_kit_cookie);
    }
}

void
session_unlock (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);

    if (getuid () == 0)
    {
        if (priv->login1_session_id)
            login1_service_unlock_session (login1_service_get_instance (), priv->login1_session_id);
        else if (priv->console_kit_cookie)
            ck_unlock_session (priv->console_kit_cookie);
    }
}

void
session_activate (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);

    if (getuid () == 0)
    {
        if (priv->login1_session_id)
            login1_service_activate_session (login1_service_get_instance (), priv->login1_session_id);
        else if (priv->console_kit_cookie)
            ck_activate_session (priv->console_kit_cookie);
    }
}

void
session_stop (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);

    if (priv->stopping)
        return;
    priv->stopping = TRUE;

    /* Kill remaining processes in our logind session to avoid them leaking
     * to the user session (they share the same $DISPLAY) */
    if (getuid () == 0 && priv->login1_session_id)
        login1_service_terminate_session (login1_service_get_instance (), priv->login1_session_id);

    /* If can cleanly stop then do that */
    if (session_get_is_authenticated (session) && !priv->command_run)
    {
        priv->command_run = TRUE;
        write_string (session, NULL); // log filename
        LogMode log_mode = LOG_MODE_INVALID;
        write_data (session, &log_mode, sizeof (log_mode)); // log mode
        write_string (session, NULL); // tty
        write_string (session, NULL); // xauth filename
        write_string (session, NULL); // xdisplay
        write_xauth (session, NULL); // xauth
        gsize n = 0;
        write_data (session, &n, sizeof (n)); // environment
        write_data (session, &n, sizeof (n)); // command
        return;
    }

    return SESSION_GET_CLASS (session)->stop (session);
}

static void
session_real_stop (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);

    g_return_if_fail (session != NULL);

    if (priv->pid > 0)
    {
        l_debug (session, "Sending SIGTERM");
        kill (priv->pid, SIGTERM);
        // FIXME: Handle timeout
    }
    else
        g_signal_emit (G_OBJECT (session), signals[STOPPED], 0);
}

gboolean
session_get_is_stopping (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, FALSE);
    return priv->stopping;
}

static void
session_init (Session *session)
{
    SessionPrivate *priv = session_get_instance_private (session);

    priv->log_filename = g_strdup (".xsession-errors");
    priv->log_mode = LOG_MODE_BACKUP_AND_TRUNCATE;
    priv->to_child_input = -1;
    priv->from_child_output = -1;
}

static void
session_finalize (GObject *object)
{
    Session *self = SESSION (object);
    SessionPrivate *priv = session_get_instance_private (self);

    g_clear_object (&priv->config);
    g_clear_object (&priv->display_server);
    if (priv->pid)
        kill (priv->pid, SIGKILL);
    close (priv->to_child_input);
    close (priv->from_child_output);
    g_clear_pointer (&priv->from_child_channel, g_io_channel_unref);
    if (priv->from_child_watch)
        g_source_remove (priv->from_child_watch);
    if (priv->child_watch)
        g_source_remove (priv->child_watch);
    g_clear_pointer (&priv->username, g_free);
    g_clear_pointer (&priv->home_directory, g_free);
    g_clear_object (&priv->user);
    g_clear_pointer (&priv->pam_service, g_free);
    for (size_t i = 0; i < priv->messages_length; i++)
        g_free ((char *) priv->messages[i].msg);
    g_clear_pointer (&priv->messages, g_free);
    g_clear_pointer (&priv->authentication_result_string, g_free);
    g_clear_pointer (&priv->log_filename, g_free);
    g_clear_pointer (&priv->tty, g_free);
    g_clear_pointer (&priv->xdisplay, g_free);
    g_clear_object (&priv->x_authority);
    g_clear_pointer (&priv->remote_host_name, g_free);
    g_clear_pointer (&priv->login1_session_id, g_free);
    g_clear_pointer (&priv->console_kit_cookie, g_free);
    g_list_free_full (priv->env, g_free);
    g_clear_pointer (&priv->argv, g_strfreev);

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

    signals[CREATE_GREETER] =
        g_signal_new (SESSION_SIGNAL_CREATE_GREETER,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, create_greeter),
                      g_signal_accumulator_first_wins,
                      NULL,
                      NULL,
                      GREETER_TYPE, 0);

    signals[GOT_MESSAGES] =
        g_signal_new (SESSION_SIGNAL_GOT_MESSAGES,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, got_messages),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[AUTHENTICATION_COMPLETE] =
        g_signal_new (SESSION_SIGNAL_AUTHENTICATION_COMPLETE,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, authentication_complete),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[STOPPED] =
        g_signal_new (SESSION_SIGNAL_STOPPED,
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
    SessionPrivate *priv = session_get_instance_private (session);

    if (priv->pid != 0)
        return g_snprintf (buf, buflen, "Session pid=%d: ", priv->pid);
    else
        return g_snprintf (buf, buflen, "Session: ");
}

static void
session_logger_iface_init (LoggerInterface *iface)
{
    iface->logprefix = &session_real_logprefix;
}
