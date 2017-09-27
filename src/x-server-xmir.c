/*
 * Copyright (C) 2010-2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <config.h>

#include "x-server-xmir.h"

struct XServerXmirPrivate
{
    /* Compositor we are running under */
    UnitySystemCompositor *compositor;

    /* TRUE if we are waiting for the compositor to start */
    gboolean waiting_for_compositor;

    /* ID to report to Mir */
    gchar *mir_id;

    /* Filename of socket Mir is listening on */
    gchar *mir_socket;
};

G_DEFINE_TYPE (XServerXmir, x_server_xmir, X_SERVER_LOCAL_TYPE);

static void
compositor_ready_cb (UnitySystemCompositor *compositor, XServerXmir *server)
{
    gboolean result;

    if (!server->priv->waiting_for_compositor)
        return;
    server->priv->waiting_for_compositor = FALSE;

    result = X_SERVER_LOCAL_CLASS (x_server_xmir_parent_class)->start (DISPLAY_SERVER (server));
    if (!result)
        display_server_stop (DISPLAY_SERVER (server));
}

static void
compositor_stopped_cb (UnitySystemCompositor *compositor, XServerXmir *server)
{
    display_server_stop (DISPLAY_SERVER (server));  
}

XServerXmir *
x_server_xmir_new (UnitySystemCompositor *compositor)
{
    XServerXmir *server;

    server = g_object_new (X_SERVER_XMIR_TYPE, NULL);
    x_server_local_set_command (X_SERVER_LOCAL (server), "Xmir");
    server->priv->compositor = g_object_ref (compositor);
    g_signal_connect (compositor, DISPLAY_SERVER_SIGNAL_READY, G_CALLBACK (compositor_ready_cb), server);
    g_signal_connect (compositor, DISPLAY_SERVER_SIGNAL_STOPPED, G_CALLBACK (compositor_stopped_cb), server);

    return server;
}

void
x_server_xmir_set_mir_id (XServerXmir *server, const gchar *id)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->mir_id);
    server->priv->mir_id = g_strdup (id);
}

const gchar *
x_server_xmir_get_mir_id (XServerXmir *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->mir_id;
}

void
x_server_xmir_set_mir_socket (XServerXmir *server, const gchar *socket)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->mir_socket);
    server->priv->mir_socket = g_strdup (socket);
}

static void
x_server_xmir_add_args (XServerLocal *x_server, GString *command)
{
    XServerXmir *server = X_SERVER_XMIR (x_server);

    if (server->priv->mir_id)
        g_string_append_printf (command, " -mir %s", server->priv->mir_id);

    if (server->priv->mir_socket)
        g_string_append_printf (command, " -mirSocket %s", server->priv->mir_socket);
}

static DisplayServer *
x_server_xmir_get_parent (DisplayServer *server)
{
    return DISPLAY_SERVER (X_SERVER_XMIR (server)->priv->compositor);
}

static gint
x_server_xmir_get_vt (DisplayServer *server)
{
    return display_server_get_vt (DISPLAY_SERVER (X_SERVER_XMIR (server)->priv->compositor));
}

static gboolean
x_server_xmir_start (DisplayServer *display_server)
{
    XServerXmir *server = X_SERVER_XMIR (display_server);

    if (display_server_get_is_ready (DISPLAY_SERVER (server->priv->compositor)))
        return X_SERVER_LOCAL_CLASS (x_server_xmir_parent_class)->start (display_server);
    else
    {
        if (!server->priv->waiting_for_compositor)
        {
            server->priv->waiting_for_compositor = TRUE;
            if (!display_server_start (DISPLAY_SERVER (server->priv->compositor)))
                return FALSE;
        }
        return TRUE;
    }
}

static void
x_server_xmir_init (XServerXmir *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, X_SERVER_XMIR_TYPE, XServerXmirPrivate);
}

static void
x_server_xmir_finalize (GObject *object)
{
    XServerXmir *self = X_SERVER_XMIR (object);

    if (self->priv->compositor)
        g_signal_handlers_disconnect_matched (self->priv->compositor, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    g_clear_object (&self->priv->compositor);
    g_clear_pointer (&self->priv->mir_id, g_free);
    g_clear_pointer (&self->priv->mir_socket, g_free);

    G_OBJECT_CLASS (x_server_xmir_parent_class)->finalize (object);
}

static void
x_server_xmir_class_init (XServerXmirClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);
    XServerLocalClass *x_server_local_class = X_SERVER_LOCAL_CLASS (klass);  

    x_server_local_class->add_args = x_server_xmir_add_args;
    display_server_class->get_parent = x_server_xmir_get_parent;
    display_server_class->get_vt = x_server_xmir_get_vt;
    display_server_class->start = x_server_xmir_start;
    object_class->finalize = x_server_xmir_finalize;

    g_type_class_add_private (klass, sizeof (XServerXmirPrivate));
}
