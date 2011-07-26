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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <grp.h>

#include "session.h"
#include "configuration.h"

struct SessionPrivate
{
    /* Authentication for this session */
    PAMSession *authentication;
  
    /* Command to run for this session */
    gchar *command;

    /* Cookie for the session */
    gchar *cookie;

    /* TRUE if this is a greeter session */
    gboolean is_greeter;
};

G_DEFINE_TYPE (Session, session, PROCESS_TYPE);

void
session_set_authentication (Session *session, PAMSession *authentication)
{
    g_return_if_fail (session != NULL);
    session->priv->authentication = g_object_ref (authentication);
}

PAMSession *
session_get_authentication (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->authentication;
}

User *
session_get_user (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return pam_session_get_user (session->priv->authentication);
}

void
session_set_is_greeter (Session *session, gboolean is_greeter)
{
    g_return_if_fail (session != NULL);
    session->priv->is_greeter = is_greeter;
}

gboolean
session_get_is_greeter (Session *session)
{
    g_return_val_if_fail (session != NULL, FALSE);
    return session->priv->is_greeter;
}

void
session_set_command (Session *session, const gchar *command)
{
    g_return_if_fail (session != NULL);

    g_free (session->priv->command);
    session->priv->command = g_strdup (command);
}

const gchar *
session_get_command (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);  
    return session->priv->command;
}

void
session_set_cookie (Session *session, const gchar *cookie)
{
    g_return_if_fail (session != NULL);

    g_free (session->priv->cookie);
    session->priv->cookie = g_strdup (cookie);
}

const gchar *
session_get_cookie (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);  
    return session->priv->cookie;
}

static gchar *
get_absolute_command (const gchar *command)
{
    gchar **tokens;
    gchar *absolute_binary, *absolute_command = NULL;

    tokens = g_strsplit (command, " ", 2);

    absolute_binary = g_find_program_in_path (tokens[0]);
    if (absolute_binary)
    {
        if (tokens[1])
            absolute_command = g_strjoin (" ", absolute_binary, tokens[1], NULL);
        else
            absolute_command = g_strdup (absolute_binary);
    }

    g_strfreev (tokens);

    return absolute_command;
}

static void
set_env_from_authentication (Session *session, PAMSession *authentication)
{
    gchar **pam_env;

    pam_env = pam_session_get_envlist (authentication);
    if (pam_env)
    {
        gchar *env_string;      
        int i;

        env_string = g_strjoinv (" ", pam_env);
        g_debug ("PAM returns environment '%s'", env_string);
        g_free (env_string);

        for (i = 0; pam_env[i]; i++)
        {
            gchar **pam_env_vars = g_strsplit (pam_env[i], "=", 2);
            if (pam_env_vars && pam_env_vars[0] && pam_env_vars[1])
                process_set_env (PROCESS (session), pam_env_vars[0], pam_env_vars[1]);
            else
                g_warning ("Can't parse PAM environment variable %s", pam_env[i]);
            g_strfreev (pam_env_vars);
        }
        g_strfreev (pam_env);
    }
}

static gboolean
session_real_start (Session *session)
{
    //gint session_stdin, session_stdout, session_stderr;
    gboolean result;
    User *user;
    gchar *absolute_command;
    GError *error = NULL;

    g_return_val_if_fail (session->priv->authentication != NULL, FALSE);
    g_return_val_if_fail (session->priv->command != NULL, FALSE);

    absolute_command = get_absolute_command (session->priv->command);
    if (!absolute_command)
    {
        g_debug ("Can't launch session %s, not found in path", session->priv->command);
        return FALSE;
    }

    pam_session_open (session->priv->authentication);

    g_debug ("Launching session");

    user = pam_session_get_user (session->priv->authentication);
    process_set_env (PROCESS (session), "PATH", "/usr/local/bin:/usr/bin:/bin");
    process_set_env (PROCESS (session), "USER", user_get_name (user));
    process_set_env (PROCESS (session), "USERNAME", user_get_name (user)); // FIXME: Is this required?
    process_set_env (PROCESS (session), "HOME", user_get_home_directory (user));
    process_set_env (PROCESS (session), "SHELL", user_get_shell (user));
    set_env_from_authentication (session, session->priv->authentication);

    if (session->priv->cookie)
        process_set_env (PROCESS (session), "XDG_SESSION_COOKIE", session->priv->cookie);

    result = process_start (PROCESS (session),
                            user,
                            user_get_home_directory (user),
                            absolute_command,
                            &error);
    g_free (absolute_command);

    if (!result)
        g_warning ("Failed to spawn session: %s", error->message);
    g_clear_error (&error);

    return result;
}

gboolean
session_start (Session *session)
{
    g_return_val_if_fail (session != NULL, FALSE); 
    return SESSION_GET_CLASS (session)->start (session);
}

static void
session_real_stop (Session *session)
{
    process_signal (PROCESS (session), SIGTERM);
}

void
session_stop (Session *session)
{
    g_return_if_fail (session != NULL);
    SESSION_GET_CLASS (session)->stop (session);
}

static void
session_stopped (Process *process)
{
    Session *session = SESSION (process);

    pam_session_close (session->priv->authentication);

    PROCESS_CLASS (session_parent_class)->stopped (process);
}

static void
session_init (Session *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, SESSION_TYPE, SessionPrivate);
}

static void
session_finalize (GObject *object)
{
    Session *self;

    self = SESSION (object);

    if (self->priv->authentication)
        g_object_unref (self->priv->authentication);
    g_free (self->priv->command);
    g_free (self->priv->cookie);

    G_OBJECT_CLASS (session_parent_class)->finalize (object);
}

static void
session_class_init (SessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    ProcessClass *process_class = PROCESS_CLASS (klass);

    klass->start = session_real_start;
    klass->stop = session_real_stop;
    process_class->stopped = session_stopped;
    object_class->finalize = session_finalize;

    g_type_class_add_private (klass, sizeof (SessionPrivate));
}
