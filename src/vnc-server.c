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

typedef struct
{
    /* Port to listen on */
    guint port;

    /* Address to listen on */
    gchar *listen_address;

    /* Listening sockets */
    GSocket *socket, *socket6;
} VNCServerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (VNCServer, vnc_server, G_TYPE_OBJECT)

VNCServer *
vnc_server_new (void)
{
    return g_object_new (VNC_SERVER_TYPE, NULL);
}

void
vnc_server_set_port (VNCServer *server, guint port)
{
    VNCServerPrivate *priv = vnc_server_get_instance_private (server);
    g_return_if_fail (server != NULL);
    priv->port = port;
}

guint
vnc_server_get_port (VNCServer *server)
{
    VNCServerPrivate *priv = vnc_server_get_instance_private (server);
    g_return_val_if_fail (server != NULL, 0);
    return priv->port;
}

void
vnc_server_set_listen_address (VNCServer *server, const gchar *listen_address)
{
    VNCServerPrivate *priv = vnc_server_get_instance_private (server);

    g_return_if_fail (server != NULL);

    g_free (priv->listen_address);
    priv->listen_address = g_strdup (listen_address);
}

const gchar *
vnc_server_get_listen_address (VNCServer *server)
{
    VNCServerPrivate *priv = vnc_server_get_instance_private (server);
    g_return_val_if_fail (server != NULL, NULL);
    return priv->listen_address;
}

static gboolean
read_cb (GSocket *socket, GIOCondition condition, VNCServer *server)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GSocket) client_socket = g_socket_accept (socket, NULL, &error);
    if (error)
        g_warning ("Failed to get connection from from VNC socket: %s", error->message);

    if (client_socket)
    {
        GInetSocketAddress *address = G_INET_SOCKET_ADDRESS (g_socket_get_remote_address (client_socket, NULL));
        g_autofree gchar *hostname = g_inet_address_to_string (g_inet_socket_address_get_address (address));
        g_debug ("Got VNC connection from %s:%d", hostname, g_inet_socket_address_get_port (address));

        g_signal_emit (server, signals[NEW_CONNECTION], 0, client_socket);
    }

    return TRUE;
}

static GSocket *
open_tcp_socket (GSocketFamily family, guint port, const gchar *listen_address, GError **error)
{
    g_autoptr(GSocket) socket = g_socket_new (family, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, error);
    if (!socket)
        return NULL;

    g_autoptr(GSocketAddress) address = NULL;
    if (listen_address)
    {
        GList *addresses = g_resolver_lookup_by_name (g_resolver_get_default (), listen_address, NULL, error);
        if (!addresses)
            return NULL;
        address = g_inet_socket_address_new (addresses->data, port);
        g_resolver_free_addresses (addresses);
    }
    else
        address = g_inet_socket_address_new (g_inet_address_new_any (family), port);
    if (!g_socket_bind (socket, address, TRUE, error) ||
        !g_socket_listen (socket, error))
        return NULL;

    return g_steal_pointer (&socket);
}

gboolean
vnc_server_start (VNCServer *server)
{
    VNCServerPrivate *priv = vnc_server_get_instance_private (server);

    g_return_val_if_fail (server != NULL, FALSE);

    // Bind to IPv6 first, as this implies binding to 0.0.0.0 in the
    // Linux kernel default configuration, which would otherwise cause
    // IPv6 clients to fail with "Error binding to address [::]:5900:
    // Address already in use" (#266).
    g_autoptr(GError) ipv6_error = NULL;
    priv->socket6 = open_tcp_socket (G_SOCKET_FAMILY_IPV6, priv->port, priv->listen_address, &ipv6_error);
    if (ipv6_error)
        g_warning ("Failed to create IPv6 VNC socket: %s", ipv6_error->message);

    if (priv->socket6)
    {
        GSource *source = g_socket_create_source (priv->socket6, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }

    g_autoptr(GError) ipv4_error = NULL;
    priv->socket = open_tcp_socket (G_SOCKET_FAMILY_IPV4, priv->port, priv->listen_address, &ipv4_error);
    if (ipv4_error)
        g_warning ("Failed to create IPv4 VNC socket: %s", ipv4_error->message);

    if (priv->socket)
    {
        GSource *source = g_socket_create_source (priv->socket, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }

    if (!priv->socket && !priv->socket6)
        return FALSE;

    return TRUE;
}

static void
vnc_server_init (VNCServer *server)
{
    VNCServerPrivate *priv = vnc_server_get_instance_private (server);
    priv->port = 5900;
}

static void
vnc_server_finalize (GObject *object)
{
    VNCServer *self = VNC_SERVER (object);
    VNCServerPrivate *priv = vnc_server_get_instance_private (self);

    g_clear_pointer (&priv->listen_address, g_free);
    g_clear_object (&priv->socket);
    g_clear_object (&priv->socket6);

    G_OBJECT_CLASS (vnc_server_parent_class)->finalize (object);
}

static void
vnc_server_class_init (VNCServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = vnc_server_finalize;

    signals[NEW_CONNECTION] =
        g_signal_new (VNC_SERVER_SIGNAL_NEW_CONNECTION,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (VNCServerClass, new_connection),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_SOCKET);
}
