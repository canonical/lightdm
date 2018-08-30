/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include "x-server.h"

#define MAXIMUM_REQUEST_LENGTH 65535

enum {
    X_SERVER_CLIENT_CONNECTED,
    X_SERVER_CLIENT_DISCONNECTED,
    X_SERVER_RESET,
    X_SERVER_LAST_SIGNAL
};
static guint x_server_signals[X_SERVER_LAST_SIGNAL] = { 0 };

typedef struct
{
    gint display_number;

    gchar *socket_path;
    GSocket *socket;
    GIOChannel *channel;
    GHashTable *clients;
} XServerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (XServer, x_server, G_TYPE_OBJECT)

typedef struct
{
    XServer *server;
    GSocket *socket;
    GIOChannel *channel;
} XClientPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (XClient, x_client, G_TYPE_OBJECT)

enum
{
    X_CLIENT_DISCONNECTED,
    X_CLIENT_LAST_SIGNAL
};
static guint x_client_signals[X_CLIENT_LAST_SIGNAL] = { 0 };

void
x_client_send_failed (XClient *client, const gchar *reason)
{
    XClientPrivate *priv = x_client_get_instance_private (client);
    g_autofree gchar *message = g_strdup_printf ("FAILED:%s", reason);
    errno = 0;
    if (send (g_io_channel_unix_get_fd (priv->channel), message, strlen (message), 0) != strlen (message))
        g_printerr ("Failed to send FAILED: %s\n", strerror (errno));
}

void
x_client_send_success (XClient *client)
{
    XClientPrivate *priv = x_client_get_instance_private (client);
    g_autofree gchar *message = g_strdup ("SUCCESS");
    errno = 0;
    if (send (g_io_channel_unix_get_fd (priv->channel), message, strlen (message), 0) != strlen (message))
        g_printerr ("Failed to send SUCCESS: %s\n", strerror (errno));
}

void
x_client_disconnect (XClient *client)
{
    XClientPrivate *priv = x_client_get_instance_private (client);
    g_io_channel_shutdown (priv->channel, TRUE, NULL);
}

static void
x_client_init (XClient *client)
{
}

static void
x_client_class_init (XClientClass *klass)
{
    x_client_signals[X_CLIENT_DISCONNECTED] =
        g_signal_new (X_CLIENT_SIGNAL_DISCONNECTED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, disconnected),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

XServer *
x_server_new (gint display_number)
{
    XServer *server = g_object_new (x_server_get_type (), NULL);
    XServerPrivate *priv = x_server_get_instance_private (server);
    priv->display_number = display_number;
    return server;
}

static gboolean
client_read_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    XClient *client = data;
    XClientPrivate *priv = x_client_get_instance_private (client);

    g_autofree gchar *d = NULL;
    gsize d_length;
    if (g_io_channel_read_to_end (channel, &d, &d_length, NULL) == G_IO_STATUS_NORMAL && d_length == 0)
    {
        XServer *server = priv->server;
        XServerPrivate *s_priv = x_server_get_instance_private (server);

        g_signal_emit (client, x_client_signals[X_CLIENT_DISCONNECTED], 0);
        g_signal_emit (server, x_server_signals[X_SERVER_CLIENT_DISCONNECTED], 0, client);

        g_hash_table_remove (s_priv->clients, priv->channel);

        if (g_hash_table_size (s_priv->clients) == 0)
            g_signal_emit (server, x_server_signals[X_SERVER_RESET], 0);

        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static gboolean
socket_connect_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    XServer *server = data;
    XServerPrivate *priv = x_server_get_instance_private (server);

    g_autoptr(GError) error = NULL;
    g_autoptr(GSocket) data_socket = g_socket_accept (priv->socket, NULL, &error);
    if (error)
        g_warning ("Error accepting connection: %s", strerror (errno));
    if (!data_socket)
        return FALSE;

    XClient *client = g_object_new (x_client_get_type (), NULL);
    XClientPrivate *c_priv = x_client_get_instance_private (client);
    c_priv->server = server;
    c_priv->socket = g_steal_pointer (&data_socket);
    c_priv->channel = g_io_channel_unix_new (g_socket_get_fd (c_priv->socket));
    g_io_add_watch (c_priv->channel, G_IO_IN | G_IO_HUP, client_read_cb, client);
    g_hash_table_insert (priv->clients, c_priv->channel, client);

    g_signal_emit (server, x_server_signals[X_SERVER_CLIENT_CONNECTED], 0, client);

    return TRUE;
}

gboolean
x_server_start (XServer *server)
{
    XServerPrivate *priv = x_server_get_instance_private (server);

    g_autofree gchar *name = g_strdup_printf (".x:%d", priv->display_number);
    priv->socket_path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), name, NULL);

    g_autoptr(GError) error = NULL;
    priv->socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
    if (!priv->socket ||
        !g_socket_bind (priv->socket, g_unix_socket_address_new (priv->socket_path), TRUE, &error) ||
        !g_socket_listen (priv->socket, &error))
    {
        g_warning ("Error creating Unix X socket: %s", error->message);
        return FALSE;
    }
    priv->channel = g_io_channel_unix_new (g_socket_get_fd (priv->socket));
    g_io_add_watch (priv->channel, G_IO_IN, socket_connect_cb, server);

    return TRUE;
}

gsize
x_server_get_n_clients (XServer *server)
{
    XServerPrivate *priv = x_server_get_instance_private (server);
    return g_hash_table_size (priv->clients);
}

static void
x_server_init (XServer *server)
{
    XServerPrivate *priv = x_server_get_instance_private (server);
    priv->clients = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) g_io_channel_unref, g_object_unref);
}

static void
x_server_finalize (GObject *object)
{
    XServer *server = X_SERVER (object);
    XServerPrivate *priv = x_server_get_instance_private (server);

    if (priv->socket_path)
        unlink (priv->socket_path);
    G_OBJECT_CLASS (x_server_parent_class)->finalize (object);
}

static void
x_server_class_init (XServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = x_server_finalize;

    x_server_signals[X_SERVER_CLIENT_CONNECTED] =
        g_signal_new (X_SERVER_SIGNAL_CLIENT_CONNECTED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XServerClass, client_connected),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, x_client_get_type ());
    x_server_signals[X_SERVER_CLIENT_DISCONNECTED] =
        g_signal_new (X_SERVER_SIGNAL_CLIENT_DISCONNECTED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XServerClass, client_disconnected),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, x_client_get_type ());
    x_server_signals[X_SERVER_RESET] =
        g_signal_new (X_SERVER_SIGNAL_RESET,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XServerClass, reset),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}
