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
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <grp.h>

#include "session.h"

enum {
    EXITED,
    KILLED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct SessionPrivate
{
    /* User running this session */
    gchar *username;
  
    /* Info from password database */
    uid_t uid;
    gid_t gid;
  
    /* Path of file to log to */
    gchar *log_file;

    /* Environment variables */
    GHashTable *env;

    /* Command to run for this session */
    gchar *command;

    /* Session process */
    GPid pid;

    /* X authorization */
    XAuthorization *authorization;
    gchar *authorization_path;
    GFile *authorization_file;
};

G_DEFINE_TYPE (Session, session, G_TYPE_OBJECT);

Session *
session_new (const char *username, const char *command)
{
    Session *self = g_object_new (SESSION_TYPE, NULL);

    self->priv->username = g_strdup (username);
    self->priv->command = g_strdup (command);

    return self;
}

const gchar *
session_get_username (Session *session)
{
    return session->priv->username;
}

const gchar *
session_get_command (Session *session)
{
    return session->priv->command;
}

void
session_set_log_file (Session *session, const gchar *log_file)
{
    g_free (session->priv->log_file);
    session->priv->log_file = g_strdup (log_file);
}

const gchar *
session_get_log_file (Session *session)
{
    return session->priv->log_file;
}

void
session_set_env (Session *session, const gchar *name, const gchar *value)
{
    g_hash_table_insert (session->priv->env, g_strdup (name), g_strdup (value));
}

void
session_set_authorization (Session *session, XAuthorization *authorization, const gchar *path)
{
    session->priv->authorization = g_object_ref (authorization);
    session->priv->authorization_path = g_strdup (path);
}

XAuthorization *session_get_authorization (Session *session)
{
    return session->priv->authorization;
}

static void
session_watch_cb (GPid pid, gint status, gpointer data)
{
    Session *session = data;

    session->priv->pid = 0;

    if (WIFEXITED (status))
    {
        g_debug ("Session exited with return value %d", WEXITSTATUS (status));
        g_signal_emit (session, signals[EXITED], 0, WEXITSTATUS (status));
    }
    else if (WIFSIGNALED (status))
    {
        g_debug ("Session terminated with signal %d", WTERMSIG (status));
        g_signal_emit (session, signals[KILLED], 0, WTERMSIG (status));
    }
}

static void
session_fork_cb (gpointer data)
{
    Session *session = data;
    GHashTableIter iter;
    gpointer key, value;
    int fd;

    /* Set environment */
    g_hash_table_iter_init (&iter, session->priv->env);
    while (g_hash_table_iter_next (&iter, &key, &value))
        g_setenv ((gchar *)key, (gchar *)value, TRUE);

    if (initgroups (session->priv->username, session->priv->gid) < 0)
    {
        g_warning ("Failed to initialize supplementary groups for %s: %s", session->priv->username, strerror (errno));
        //_exit(1);
    }

    if (session->priv->gid && setgid (session->priv->gid) != 0)
    {
        g_warning ("Failed to set group ID: %s", strerror (errno));
        _exit(1);
    }

    if (session->priv->uid && setuid (session->priv->uid) != 0)
    {
        g_warning ("Failed to set user ID: %s", strerror (errno));
        _exit(1);
    }

    /* Make input non-blocking */
    fd = g_open ("/dev/null", O_RDONLY);
    dup2 (fd, STDIN_FILENO);
    close (fd);

    /* Redirect output to logfile */
    if (session->priv->log_file)
    {
        fd = g_open (session->priv->log_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0)
            g_warning ("Failed to open session log file %s: %s", session->priv->log_file, g_strerror (errno));
        else
        {
            dup2 (fd, STDOUT_FILENO);
            dup2 (fd, STDERR_FILENO);
            close (fd);
        }
    }
}

static gchar *
make_env_string (Session *session)
{
    GString *string;
    gchar *result;
    gpointer key, value;
    GHashTableIter iter;

    string = g_string_new ("");
    g_hash_table_iter_init (&iter, session->priv->env);
    if (g_hash_table_iter_next (&iter, &key, &value))
    {
        g_string_append_printf (string, "%s=%s", (gchar *)key, (gchar *)value);      
        while (g_hash_table_iter_next (&iter, &key, &value))
            g_string_append_printf (string, " %s=%s", (gchar *)key, (gchar *)value);
    }
    result = string->str;
    g_string_free (string, FALSE);

    return result;
}

gboolean
session_start (Session *session)
{
    //gint session_stdin, session_stdout, session_stderr;
    gboolean result;
    gint argc;
    struct passwd *user_info;
    gchar *username, *working_dir, *env_string;
    gchar **argv;
    GError *error = NULL;
  
    g_return_val_if_fail (session->priv->pid == 0, FALSE);

    errno = 0;
    if (session->priv->username)
    {
        user_info = getpwnam (session->priv->username);
        if (!user_info)
        {
            if (errno == 0)
                g_warning ("Unable to get information on user %s: User does not exist", session->priv->username);
            else
                g_warning ("Unable to get information on user %s: %s", session->priv->username, strerror (errno));
            return FALSE;
        }
    }
    else
    {
        user_info = getpwuid (getuid ());
        if (!user_info)
        {
            g_warning ("Unable to determine current username: %s", strerror (errno));
            return FALSE;;
        }
    }

    session->priv->uid = user_info->pw_uid;
    session->priv->gid = user_info->pw_gid;

    username = g_strdup (user_info->pw_name);
    working_dir = g_strdup (user_info->pw_dir);
    session_set_env (session, "USER", user_info->pw_name);
    session_set_env (session, "USERNAME", user_info->pw_name); // FIXME: Is this required?      
    session_set_env (session, "HOME", user_info->pw_dir);
    session_set_env (session, "SHELL", user_info->pw_shell);

    if (session->priv->authorization)
    {
        session->priv->authorization_file = xauth_write (session->priv->authorization, username, session->priv->authorization_path, &error);
        if (session->priv->authorization_file)
            session_set_env (session, "XAUTHORITY", session->priv->authorization_path);
        else
            g_warning ("Failed to write authorization: %s", error->message);
        g_clear_error (&error);
    }
    g_free (username);

    result = g_shell_parse_argv (session->priv->command, &argc, &argv, &error);
    if (!result)
        g_warning ("Failed to parse session command line: %s", error->message);
    g_clear_error (&error);
    if (!result)
    {
        g_free (working_dir);
        return FALSE;
    }

    env_string = make_env_string (session);
    g_debug ("Launching session: %s %s", env_string, session->priv->command);
    g_free (env_string);

    /* Create the log file owned by the target user */
    if (session->priv->username)
    {
        gint fd = g_open (session->priv->log_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        close (fd);
        if (chown (session->priv->log_file, session->priv->uid, session->priv->gid) != 0)
            g_warning ("Failed to set greeter log file ownership: %s", strerror (errno));
    }

    result = g_spawn_async (working_dir,
                            argv,
                            NULL,
                            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                            session_fork_cb, session,
                            &session->priv->pid,
                            &error);
    g_free (working_dir);

    if (!result)
        g_warning ("Failed to spawn session: %s", error->message);
    else
        g_child_watch_add (session->priv->pid, session_watch_cb, session);
    g_clear_error (&error);

    return session->priv->pid != 0;
}

void
session_stop (Session *session)
{
    if (session->priv->pid)
        kill (session->priv->pid, SIGTERM);
}

static void
session_init (Session *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, SESSION_TYPE, SessionPrivate);
    session->priv->env = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
session_finalize (GObject *object)
{
    Session *self;

    self = SESSION (object);
    if (self->priv->pid)
        kill (self->priv->pid, SIGTERM);
    g_free (self->priv->username);
    g_hash_table_unref (self->priv->env);
    g_free (self->priv->command);
    if (self->priv->authorization)
        g_object_unref (self->priv->authorization);
    g_free (self->priv->authorization_path);
    if (self->priv->authorization_file)
    {
        g_file_delete (self->priv->authorization_file, NULL, NULL);
        g_object_unref (self->priv->authorization_file);
    }
}

static void
session_class_init (SessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = session_finalize;

    g_type_class_add_private (klass, sizeof (SessionPrivate));

    signals[EXITED] =
        g_signal_new ("exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, exited),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__INT,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[KILLED] =
        g_signal_new ("killed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SessionClass, killed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__INT,
                      G_TYPE_NONE, 1, G_TYPE_INT);
}
