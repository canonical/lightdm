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

#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "greeter-session.h"

struct GreeterSessionPrivate
{
    /* Greeter running inside this session */
    Greeter *greeter;
};

G_DEFINE_TYPE (GreeterSession, greeter_session, SESSION_TYPE)

GreeterSession *
greeter_session_new (void)
{
    return g_object_new (GREETER_SESSION_TYPE, NULL);
}

Greeter *
greeter_session_get_greeter (GreeterSession *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->greeter;
}

static gboolean
greeter_session_start (Session *session)
{
    GreeterSession *s = GREETER_SESSION (session);
    int to_greeter_pipe[2], from_greeter_pipe[2];
    int to_greeter_input, to_greeter_output, from_greeter_input, from_greeter_output;
    g_autofree gchar *to_server_value = NULL;
    g_autofree gchar *from_server_value = NULL;
    gboolean result;

    /* Create a pipe to talk with the greeter */
    if (pipe (to_greeter_pipe) != 0 || pipe (from_greeter_pipe) != 0)
    {
        g_warning ("Failed to create pipes: %s", strerror (errno));
        return FALSE;
    }

    to_greeter_input = to_greeter_pipe[1];  
    to_greeter_output = to_greeter_pipe[0];
    from_greeter_input = from_greeter_pipe[1];
    from_greeter_output = from_greeter_pipe[0];  
    greeter_set_file_descriptors (s->priv->greeter, to_greeter_input, from_greeter_output);

    /* Don't allow the daemon end of the pipes to be accessed in child processes */
    fcntl (to_greeter_input, F_SETFD, FD_CLOEXEC);
    fcntl (from_greeter_output, F_SETFD, FD_CLOEXEC);

    /* Let the greeter session know how to communicate with the daemon */
    to_server_value = g_strdup_printf ("%d", from_greeter_input);
    session_set_env (session, "LIGHTDM_TO_SERVER_FD", to_server_value);
    from_server_value = g_strdup_printf ("%d", to_greeter_output);
    session_set_env (session, "LIGHTDM_FROM_SERVER_FD", from_server_value);

    result = SESSION_CLASS (greeter_session_parent_class)->start (session);

    /* Close the session ends of the pipe */
    close (from_greeter_input);
    close (to_greeter_output);

    return result;
}

static void
greeter_session_stop (Session *session)
{
    GreeterSession *s = GREETER_SESSION (session);

    greeter_stop (s->priv->greeter);

    SESSION_CLASS (greeter_session_parent_class)->stop (session);
}

static void
greeter_session_init (GreeterSession *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, GREETER_SESSION_TYPE, GreeterSessionPrivate);
    session->priv->greeter = greeter_new ();
}

static void
greeter_session_finalize (GObject *object)
{
    GreeterSession *self = GREETER_SESSION (object);

    g_clear_object (&self->priv->greeter);

    G_OBJECT_CLASS (greeter_session_parent_class)->finalize (object);
}

static void
greeter_session_class_init (GreeterSessionClass *klass)
{
    SessionClass *session_class = SESSION_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    session_class->start = greeter_session_start;
    session_class->stop = greeter_session_stop;  
    object_class->finalize = greeter_session_finalize;

    g_type_class_add_private (klass, sizeof (GreeterSessionPrivate));
}
