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
    /* User running this session */
    User *user;
  
    /* Command to run for this session */
    gchar *command;

    /* Cookie for the session */
    gchar *cookie;

    /* TRUE if this is a greeter session */
    gboolean is_greeter;
};

G_DEFINE_TYPE (Session, session, PROCESS_TYPE);

void
session_set_user (Session *session, User *user)
{
    g_return_if_fail (session != NULL);

    if (session->priv->user)
        g_object_unref (session->priv->user);
    session->priv->user = g_object_ref (user);

    process_set_env (PROCESS (session), "PATH", "/usr/local/bin:/usr/bin:/bin");
    process_set_env (PROCESS (session), "USER", user_get_name (user));
    process_set_env (PROCESS (session), "USERNAME", user_get_name (user)); // FIXME: Is this required?
    process_set_env (PROCESS (session), "HOME", user_get_home_directory (user));
    process_set_env (PROCESS (session), "SHELL", user_get_shell (user));
}

User *
session_get_user (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->user;
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

static gboolean
session_real_start (Session *session)
{
    //gint session_stdin, session_stdout, session_stderr;
    gboolean result;
    GError *error = NULL;
    gchar *absolute_command;

    g_return_val_if_fail (session->priv->user != NULL, FALSE);
    g_return_val_if_fail (session->priv->command != NULL, FALSE);

    absolute_command = get_absolute_command (session->priv->command);
    if (!absolute_command)
    {
        g_debug ("Can't launch session %s, not found in path", session->priv->command);
        return FALSE;
    }

    g_debug ("Launching session");

    if (session->priv->cookie)
        process_set_env (PROCESS (session), "XDG_SESSION_COOKIE", session->priv->cookie);

    result = process_start (PROCESS (session),
                            session->priv->user,
                            user_get_home_directory (session->priv->user),
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
session_init (Session *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, SESSION_TYPE, SessionPrivate);
}

static void
session_finalize (GObject *object)
{
    Session *self;

    self = SESSION (object);

    if (self->priv->user)
        g_object_unref (self->priv->user);
    g_free (self->priv->command);
    g_free (self->priv->cookie);

    G_OBJECT_CLASS (session_parent_class)->finalize (object);
}

static void
session_class_init (SessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->start = session_real_start;
    klass->stop = session_real_stop;
    object_class->finalize = session_finalize;

    g_type_class_add_private (klass, sizeof (SessionPrivate));
}
