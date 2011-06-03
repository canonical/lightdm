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

struct SessionPrivate
{
    /* User running this session */
    gchar *username;
  
    /* Path of file to log to */
    gchar *log_file;

    /* Environment variables */
    GHashTable *env;

    /* Command to run for this session */
    gchar *command;

    /* X authorization */
    XAuthorization *authorization;
    gchar *authorization_path;
    GFile *authorization_file;
};

G_DEFINE_TYPE (Session, session, CHILD_PROCESS_TYPE);

Session *
session_new ()
{
    return g_object_new (SESSION_TYPE, NULL);
}

void
session_set_username (Session *session, const gchar *username)
{
    session->priv->username = g_strdup (username);
}

const gchar *
session_get_username (Session *session)
{
    return session->priv->username;
}

void
session_set_command (Session *session, const gchar *command)
{
    session->priv->command = g_strdup (command);
}

const gchar *
session_get_command (Session *session)
{
    return session->priv->command;
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

gboolean
session_start (Session *session, gboolean create_pipe)
{
    //gint session_stdin, session_stdout, session_stderr;
    gboolean result;
    struct passwd *user_info;
    gchar *username, *working_dir;
    GError *error = NULL;

    g_return_val_if_fail (session->priv->command != NULL, FALSE);

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
            return FALSE;
        }
    }

    username = g_strdup (user_info->pw_name);
    working_dir = g_strdup (user_info->pw_dir);
    child_process_set_env (CHILD_PROCESS (session), "USER", user_info->pw_name);
    child_process_set_env (CHILD_PROCESS (session), "USERNAME", user_info->pw_name); // FIXME: Is this required?      
    child_process_set_env (CHILD_PROCESS (session), "HOME", user_info->pw_dir);
    child_process_set_env (CHILD_PROCESS (session), "SHELL", user_info->pw_shell);

    if (session->priv->authorization)
    {
        session->priv->authorization_file = xauth_write (session->priv->authorization, username, session->priv->authorization_path, &error);
        if (session->priv->authorization_file)
            child_process_set_env (CHILD_PROCESS (session), "XAUTHORITY", session->priv->authorization_path);
        else
            g_warning ("Failed to write authorization: %s", error->message);
        g_clear_error (&error);
    }
    g_free (username);

    g_debug ("Launching session");

    result = child_process_start (CHILD_PROCESS (session),
                                  session->priv->username,
                                  working_dir,
                                  session->priv->command,
                                  create_pipe,
                                  &error);
    g_free (working_dir);

    if (!result)
        g_warning ("Failed to spawn session: %s", error->message);
    g_clear_error (&error);

    return result;
}

void
session_stop (Session *session)
{
    child_process_signal (CHILD_PROCESS (session), SIGTERM);
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

    G_OBJECT_CLASS (session_parent_class)->finalize (object);
}

static void
session_class_init (SessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = session_finalize;

    g_type_class_add_private (klass, sizeof (SessionPrivate));
}
