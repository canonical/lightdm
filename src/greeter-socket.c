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

#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include "greeter-socket.h"

enum {
    CREATE_GREETER,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct GreeterSocketPrivate
{
    /* Path of socket to use */
    gchar *path;

    /* Listening UNIX socket */
    GSocket *socket;

    /* Source for listening for connections */
    GSource *source;

    /* Socket to greeter */
    GSocket *greeter_socket;

    /* Greeter connected on this socket */
    Greeter *greeter;
};

G_DEFINE_TYPE (GreeterSocket, greeter_socket, G_TYPE_OBJECT);

GreeterSocket *
greeter_socket_new (const gchar *path)
{
    GreeterSocket *socket;

    socket = g_object_new (GREETER_SOCKET_TYPE, NULL);
    socket->priv->path = g_strdup (path);

    return socket;
}

static gboolean
greeter_connect_cb (GSocket *s, GIOCondition condition, GreeterSocket *socket)
{
    GSocket *new_socket;
    GError *error = NULL;

    new_socket = g_socket_accept (socket->priv->socket, NULL, &error);
    if (error)
        g_warning ("Failed to accept greeter connection: %s", error->message);
    g_clear_error (&error);
    if (!new_socket)
        return G_SOURCE_CONTINUE;

    /* Greeter already connected */
    if (socket->priv->greeter)
    {
        g_socket_close (new_socket, NULL);
        g_object_unref (new_socket);
        return G_SOURCE_CONTINUE;
    }

    socket->priv->greeter_socket = new_socket;
    g_signal_emit (socket, signals[CREATE_GREETER], 0, &socket->priv->greeter);
    greeter_set_file_descriptors (socket->priv->greeter, g_socket_get_fd (new_socket), g_socket_get_fd (new_socket));

    return G_SOURCE_CONTINUE;
}

gboolean
greeter_socket_start (GreeterSocket *socket, GError **error)
{
    GSocketAddress *address;
    gboolean result;

    g_return_val_if_fail (socket != NULL, FALSE);
    g_return_val_if_fail (socket->priv->socket == NULL, FALSE);  

    socket->priv->socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, error);
    if (!socket->priv->socket)
        return FALSE;

    unlink (socket->priv->path);  
    address = g_unix_socket_address_new (socket->priv->path);
    result = g_socket_bind (socket->priv->socket, address, FALSE, error);
    g_object_unref (address);
    if (!result)
        return FALSE;
    if (!g_socket_listen (socket->priv->socket, error))
        return FALSE;

    socket->priv->source = g_socket_create_source (socket->priv->socket, G_IO_IN, NULL);
    g_source_set_callback (socket->priv->source, (GSourceFunc) greeter_connect_cb, socket, NULL);
    g_source_attach (socket->priv->source, NULL);

    return TRUE;
}

static void
greeter_socket_init (GreeterSocket *socket)
{
    socket->priv = G_TYPE_INSTANCE_GET_PRIVATE (socket, GREETER_SOCKET_TYPE, GreeterSocketPrivate);
}

static void
greeter_socket_finalize (GObject *object)
{
    GreeterSocket *self = GREETER_SOCKET (object);

    if (self->priv->path)
        unlink (self->priv->path); 
    g_free (self->priv->path);
    g_clear_object (&self->priv->socket);
    g_clear_object (&self->priv->source);
    g_clear_object (&self->priv->greeter_socket);
    g_clear_object (&self->priv->greeter);

    G_OBJECT_CLASS (greeter_socket_parent_class)->finalize (object);
}

static void
greeter_socket_class_init (GreeterSocketClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = greeter_socket_finalize;

    signals[CREATE_GREETER] =
        g_signal_new (GREETER_SOCKET_SIGNAL_CREATE_GREETER,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterSocketClass, create_greeter),
                      g_signal_accumulator_first_wins,
                      NULL,
                      NULL,
                      GREETER_TYPE, 0);

    g_type_class_add_private (klass, sizeof (GreeterSocketPrivate));
}
