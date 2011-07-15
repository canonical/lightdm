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
#include <fcntl.h>
#include <glib/gstdio.h>
#include <grp.h>

#include "session.h"

struct SessionPrivate
{
    /* User running this session */
    User *user;
  
    /* Command to run for this session */
    gchar *command;

    /* X authorization */
    XAuthorization *authorization;
    GFile *authorization_file;
};

G_DEFINE_TYPE (Session, session, CHILD_PROCESS_TYPE);

Session *
session_new ()
{
    return g_object_new (SESSION_TYPE, NULL);
}

void
session_set_user (Session *session, User *user)
{
    g_return_if_fail (session != NULL);

    if (session->priv->user)
        g_object_unref (session->priv->user);
    session->priv->user = g_object_ref (user);
}

User *
session_get_user (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->user;
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
session_set_authorization (Session *session, XAuthorization *authorization)
{
    g_return_if_fail (session != NULL);

    if (session->priv->authorization)
        g_object_unref (session->priv->authorization);
    session->priv->authorization = g_object_ref (authorization);
}

XAuthorization *
session_get_authorization (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->authorization;
}

gboolean
session_start (Session *session, gboolean create_pipe)
{
    //gint session_stdin, session_stdout, session_stderr;
    gboolean result;
    GError *error = NULL;
    gchar *full_path;

    g_return_val_if_fail (session != NULL, FALSE);
    g_return_val_if_fail (session->priv->user != NULL, FALSE);
    g_return_val_if_fail (session->priv->command != NULL, FALSE);

    full_path = g_find_program_in_path (session->priv->command);
    if (!full_path)
    {
        g_debug ("Can't launch session %s, not found in path", session->priv->command);
        return FALSE;     
    }

    if (session->priv->authorization)
    {
        gchar *path;

        path = g_build_filename (user_get_home_directory (session->priv->user), ".Xauthority", NULL);
        session->priv->authorization_file = g_file_new_for_path (path);
        g_free (path);

        g_debug ("Adding session authority to %s", g_file_get_path (session->priv->authorization_file));
        if (!xauth_update (session->priv->authorization, session->priv->user, session->priv->authorization_file, &error))
            g_warning ("Failed to write authorization: %s", error->message);
        g_clear_error (&error);
    }

    g_debug ("Launching session");

    result = child_process_start (CHILD_PROCESS (session),
                                  session->priv->user,
                                  user_get_home_directory (session->priv->user),
                                  full_path,
                                  create_pipe,
                                  &error);
    g_free (full_path);

    if (!result)
        g_warning ("Failed to spawn session: %s", error->message);
    g_clear_error (&error);

    return result;
}

void
session_stop (Session *session)
{
    g_return_if_fail (session != NULL);
    child_process_signal (CHILD_PROCESS (session), SIGTERM);
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

    if (self->priv->authorization_file)
    {
        g_debug ("Removing session authority from %s", g_file_get_path (self->priv->authorization_file));
        xauth_remove (self->priv->authorization, self->priv->user, self->priv->authorization_file, NULL);
        g_object_unref (self->priv->authorization_file);
    }

    if (self->priv->user)
        g_object_unref (self->priv->user);
    g_free (self->priv->command);
    if (self->priv->authorization)
        g_object_unref (self->priv->authorization);

    G_OBJECT_CLASS (session_parent_class)->finalize (object);
}

static void
session_class_init (SessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = session_finalize;

    g_type_class_add_private (klass, sizeof (SessionPrivate));
}
