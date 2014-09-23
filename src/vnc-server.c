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

#include <gio/gio.h>

#include "vnc-server.h"

enum {
    NEW_CONNECTION,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct VNCServerPrivate
{
    /* Port to listen on */
    guint port;

    /* Listening sockets */
    GSocket *socket, *socket6;
};

G_DEFINE_TYPE (VNCServer, vnc_server, G_TYPE_OBJECT);

VNCServer *
vnc_server_new (void)
{
    return g_object_new (VNC_SERVER_TYPE, NULL);
}

void
vnc_server_set_port (VNCServer *server, guint port)
{
    g_return_if_fail (server != NULL);
    server->priv->port = port;
}

guint
vnc_server_get_port (VNCServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->port;
}

static gboolean
read_cb (GSocket *socket, GIOCondition condition, VNCServer *server)
{
    GError *error = NULL;
    GSocket *client_socket;

    client_socket = g_socket_accept (socket, NULL, &error);
    if (error)
        g_warning ("Failed to get connection from from VNC socket: %s", error->message);
    g_clear_error (&error);

    if (client_socket)
    {
        GInetSocketAddress *address;
        gchar *hostname;

        address = G_INET_SOCKET_ADDRESS (g_socket_get_remote_address (client_socket, NULL));
        hostname = g_inet_address_to_string (g_inet_socket_address_get_address (address));
        g_debug ("Got VNC connection from %s:%d", hostname, g_inet_socket_address_get_port (address));
        g_free (hostname);

        g_signal_emit (server, signals[NEW_CONNECTION], 0, client_socket);
    }

    return TRUE;
}

static GSocket *
open_tcp_socket (GSocketFamily family, guint port, GError **error)
{
    GSocket *socket;
    GSocketAddress *address;

    socket = g_socket_new (family, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, error);
    if (!socket)
        return NULL;

    address = g_inet_socket_address_new (g_inet_address_new_any (family), port);
    if (!g_socket_bind (socket, address, TRUE, error) ||
        !g_socket_listen (socket, error))
    {
        g_object_unref (socket);
        return NULL;
    }

    return socket;
}

gboolean
vnc_server_start (VNCServer *server)
{
    GSource *source;
    GError *error = NULL;

    g_return_val_if_fail (server != NULL, FALSE);

    server->priv->socket = open_tcp_socket (G_SOCKET_FAMILY_IPV4, server->priv->port, &error);
    if (error)
        g_warning ("Failed to create IPv4 VNC socket: %s", error->message);
    g_clear_error (&error);

    if (server->priv->socket)
    {
        source = g_socket_create_source (server->priv->socket, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }

    server->priv->socket6 = open_tcp_socket (G_SOCKET_FAMILY_IPV6, server->priv->port, &error);
    if (error)
        g_warning ("Failed to create IPv6 VNC socket: %s", error->message);
    g_clear_error (&error);

    if (server->priv->socket6)
    {
        source = g_socket_create_source (server->priv->socket6, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }

    if (!server->priv->socket && !server->priv->socket6)
        return FALSE;

    return TRUE;
}

static void
vnc_server_init (VNCServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, VNC_SERVER_TYPE, VNCServerPrivate);
    server->priv->port = 5900;
}

static void
vnc_server_finalize (GObject *object)
{
    VNCServer *self;

    self = VNC_SERVER (object);

    if (self->priv->socket)
        g_object_unref (self->priv->socket);
    if (self->priv->socket6)
        g_object_unref (self->priv->socket6);

    G_OBJECT_CLASS (vnc_server_parent_class)->finalize (object);
}

static void
vnc_server_class_init (VNCServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = vnc_server_finalize;

    g_type_class_add_private (klass, sizeof (VNCServerPrivate));

    signals[NEW_CONNECTION] =
        g_signal_new ("new-connection",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (VNCServerClass, new_connection),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_SOCKET);
}
