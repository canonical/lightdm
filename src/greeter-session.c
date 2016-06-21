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

#include "greeter-session.h"

struct GreeterSessionPrivate
{
    /* Greeter running inside this session */
    Greeter *greeter;

    /* Communication channels to communicate with */
    int to_greeter_input;
    int from_greeter_output;
};

G_DEFINE_TYPE (GreeterSession, greeter_session, SESSION_TYPE);

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
setup_cb (Greeter *greeter, int input_fd, int output_fd, gpointer user_data)
{
    Session *session = user_data;
    gchar *value;

    /* Let the greeter session know how to communicate with the daemon */
    value = g_strdup_printf ("%d", input_fd);
    session_set_env (session, "LIGHTDM_TO_SERVER_FD", value);
    g_free (value);
    value = g_strdup_printf ("%d", output_fd);
    session_set_env (session, "LIGHTDM_FROM_SERVER_FD", value);
    g_free (value);

    return SESSION_CLASS (greeter_session_parent_class)->start (session);
}

static gboolean
greeter_session_start (Session *session)
{
    GreeterSession *s = GREETER_SESSION (session);
    return greeter_start (s->priv->greeter, setup_cb, session);
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
    close (self->priv->to_greeter_input);
    close (self->priv->from_greeter_output);

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
