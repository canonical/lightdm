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
#include <X11/X.h>
#define HASXDMAUTH
#include <X11/Xdmcp.h>
#include <gio/gio.h>

#include "xdmcp-server.h"
#include "xdmcp-protocol.h"
#include "xdmcp-session-private.h"
#include "x-authority.h"

enum {
    NEW_SESSION,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct XDMCPServerPrivate
{
    /* Port to listen on */
    guint port;

    /* Address to listen on */
    gchar *listen_address;

    /* Listening sockets */
    GSocket *socket, *socket6;

    /* Hostname to report to client */
    gchar *hostname;

    /* Status to report to clients */
    gchar *status;

    /* XDM-AUTHENTICATION-1 key */
    gchar *key;

    /* Active XDMCP sessions */
    GHashTable *sessions;
};

G_DEFINE_TYPE (XDMCPServer, xdmcp_server, G_TYPE_OBJECT);

/* Maximum number of milliseconds client will resend manage requests before giving up */
#define MANAGE_TIMEOUT 126000

XDMCPServer *
xdmcp_server_new (void)
{
    return g_object_new (XDMCP_SERVER_TYPE, NULL);
}

void
xdmcp_server_set_port (XDMCPServer *server, guint port)
{
    g_return_if_fail (server != NULL);
    server->priv->port = port;
}

guint
xdmcp_server_get_port (XDMCPServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->port;
}

void
xdmcp_server_set_listen_address (XDMCPServer *server, const gchar *listen_address)
{
    g_return_if_fail (server != NULL);

    g_free (server->priv->listen_address);
    server->priv->listen_address = g_strdup (listen_address);
}

const gchar *
xdmcp_server_get_listen_address (XDMCPServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->listen_address;
}

void
xdmcp_server_set_hostname (XDMCPServer *server, const gchar *hostname)
{
    g_return_if_fail (server != NULL);

    g_free (server->priv->hostname);
    server->priv->hostname = g_strdup (hostname);
}

const gchar *
xdmcp_server_get_hostname (XDMCPServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->hostname;
}

void
xdmcp_server_set_status (XDMCPServer *server, const gchar *status)
{
    g_return_if_fail (server != NULL);

    g_free (server->priv->status);
    server->priv->status = g_strdup (status);
}

const gchar *
xdmcp_server_get_status (XDMCPServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->status;
}

void
xdmcp_server_set_key (XDMCPServer *server, const gchar *key)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->key);
    server->priv->key = g_strdup (key);
}

static gboolean
session_timeout_cb (XDMCPSession *session)
{
    session->priv->inactive_timeout = 0;

    g_debug ("Timing out unmanaged session %d", session->priv->id);
    g_hash_table_remove (session->priv->server->priv->sessions, GINT_TO_POINTER ((gint) session->priv->id));
    return FALSE;
}

static XDMCPSession *
add_session (XDMCPServer *server)
{
    XDMCPSession *session;
    guint16 id;

    do
    {
        id = g_random_int () & 0xFFFFFFFF;
    } while (g_hash_table_lookup (server->priv->sessions, GINT_TO_POINTER ((gint) id)));

    session = xdmcp_session_new (id);
    session->priv->server = server;
    g_hash_table_insert (server->priv->sessions, GINT_TO_POINTER ((gint) id), g_object_ref (session));
    session->priv->inactive_timeout = g_timeout_add (MANAGE_TIMEOUT, (GSourceFunc) session_timeout_cb, session);

    return session;
}

static XDMCPSession *
get_session (XDMCPServer *server, guint16 id)
{
    return g_hash_table_lookup (server->priv->sessions, GINT_TO_POINTER ((gint) id));
}

static void
send_packet (GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    guint8 data[1024];
    gssize n_written;

    g_debug ("Send %s", xdmcp_packet_tostring (packet));

    n_written = xdmcp_packet_encode (packet, data, 1024);
    if (n_written < 0)
      g_critical ("Failed to encode XDMCP packet");
    else
    {
        GError *error = NULL;

        g_socket_send_to (socket, address, (gchar *) data, n_written, NULL, &error);
        if (error)
            g_warning ("Error sending packet: %s", error->message);
        g_clear_error (&error);
    }
}

static const gchar *
get_authentication_name (XDMCPServer *server)
{
    if (server->priv->key)
        return "XDM-AUTHENTICATION-1";
    else
        return "";
}

static void
handle_query (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    XDMCPPacket *response;
    gchar **i;
    gchar *authentication_name = NULL;

    /* If no authentication requested and we are configured for none then allow */
    if (packet->Query.authentication_names[0] == NULL && server->priv->key == NULL)
        authentication_name = "";

    for (i = packet->Query.authentication_names; *i; i++)
    {
        if (strcmp (*i, get_authentication_name (server)) == 0 && server->priv->key != NULL)
        {
            authentication_name = *i;
            break;
        }
    }

    if (authentication_name)
    {
        response = xdmcp_packet_alloc (XDMCP_Willing);
        response->Willing.authentication_name = g_strdup (authentication_name);
        response->Willing.hostname = g_strdup (server->priv->hostname);
        response->Willing.status = g_strdup (server->priv->status);
    }
    else
    {
        response = xdmcp_packet_alloc (XDMCP_Unwilling);
        response->Unwilling.hostname = g_strdup (server->priv->hostname);
        if (server->priv->key)
            response->Unwilling.status = g_strdup_printf ("No matching authentication, server requires %s", get_authentication_name (server));
        else
            response->Unwilling.status = g_strdup ("Server does not support authentication");
    }

    send_packet (socket, address, response);

    xdmcp_packet_free (response);
}

static guint8
atox (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

static void
decode_key (const gchar *key, guint8 *data)
{
    gint i;

    memset (data, 0, 8);
    if (strncmp (key, "0x", 2) == 0 || strncmp (key, "0X", 2) == 0)
    {
        for (i = 0; i < 8; i++)
        {
            if (key[i*2] == '\0')
                break;
            data[i] |= atox (key[i*2]) << 8;
            if (key[i*2+1] == '\0')
                break;
            data[i] |= atox (key[i*2+1]);
        }
    }
    else
    {
        for (i = 1; i < 8 && key[i-1]; i++)
           data[i] = key[i-1];
    }
}

static GInetAddress *
connection_to_address (XDMCPConnection *connection)
{
    switch (connection->type)
    {
    case XAUTH_FAMILY_INTERNET:
        if (connection->address.length == 4)
            return g_inet_address_new_from_bytes (connection->address.data, G_SOCKET_FAMILY_IPV4);
        else
            return NULL;
    case XAUTH_FAMILY_INTERNET6:
        if (connection->address.length == 16)
            return g_inet_address_new_from_bytes (connection->address.data, G_SOCKET_FAMILY_IPV6);
        else
            return NULL;
    default:
        return NULL;
    }
}

static gssize
find_address (GInetAddress **addresses, gsize length, GSocketFamily family)
{
    int i;

    for (i = 0; i < length; i++)
    {
        GInetAddress *address = addresses[i];
        if (address && g_inet_address_get_family (address) == family)
            return i;
    }

    return -1;
}

static XDMCPConnection *
choose_connection (XDMCPPacket *packet, GInetAddress *source_address)
{
    GInetAddress **addresses;
    gsize addresses_length, i;
    gssize index = -1;

    addresses_length = packet->Request.n_connections;
    addresses = malloc (sizeof (GInetAddress *) * addresses_length);
    for (i = 0; i < addresses_length; i++)
        addresses[i] = connection_to_address (&packet->Request.connections[i]);

    /* Use the address the request came in on as this is the least likely to have firewall / routing issues */
    for (i = 0; i < addresses_length && index < 0; i++)
        if (g_inet_address_equal (source_address, addresses[i]))
            index = i;

    /* Otherwise try and find an address that matches the incoming type */
    if (index < 0)
        index = find_address (addresses, addresses_length, g_inet_address_get_family (source_address));

    /* Otherwise use the first available */
    if (index < 0 && addresses_length > 0)
        index = 0;

    for (i = 0; i < addresses_length; i++)
        g_object_unref (addresses[i]);
    g_free (addresses);

    return &packet->Request.connections[index];
}

static void
handle_request (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    XDMCPPacket *response;
    XDMCPSession *session;
    guint8 *authentication_data = NULL;
    gsize authentication_data_length = 0;
    gboolean match_authorization = FALSE;
    gchar *authorization_name;
    guint8 *authorization_data = NULL;
    gsize authorization_data_length = 0;
    guint8 *session_authorization_data = NULL;
    gsize session_authorization_data_length = 0;
    gchar **j;
    XDMCPConnection *connection;
    gchar *display_number;
    XdmAuthKeyRec rho;

    /* Choose an address to connect back on */
    connection = choose_connection (packet, g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address)));

    /* Decline if haven't got an address we can connect on */
    if (!connection)
    {
        response = xdmcp_packet_alloc (XDMCP_Decline);
        response->Decline.status = g_strdup ("No valid address found");
        response->Decline.authentication_name = g_strdup (packet->Request.authentication_name);
        response->Decline.authentication_data.data = authentication_data;
        response->Decline.authentication_data.length = authentication_data_length;
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
        return;
    }

    /* Must be using our authentication scheme */
    if (strcmp (packet->Request.authentication_name, get_authentication_name (server)) != 0)
    {
        response = xdmcp_packet_alloc (XDMCP_Decline);
        if (server->priv->key)
            response->Decline.status = g_strdup_printf ("Server only supports %s authentication", get_authentication_name (server));
        else
            response->Decline.status = g_strdup ("Server does not support authentication");
        response->Decline.authentication_name = g_strdup ("");
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
        return;
    }

    /* Perform requested authentication */
    if (server->priv->key)
    {
        guint8 input[8], key[8];

        memset (input, 0, 8);
        memcpy (input, packet->Request.authentication_data.data, packet->Request.authentication_data.length > 8 ? 8 : packet->Request.authentication_data.length);

        /* Setup key */
        decode_key (server->priv->key, key);

        /* Decode message from server */
        authentication_data = g_malloc (sizeof (guint8) * 8);
        authentication_data_length = 8;

        XdmcpUnwrap (input, key, rho.data, authentication_data_length);
        XdmcpIncrementKey (&rho);
        XdmcpWrap (rho.data, key, authentication_data, authentication_data_length);

        authorization_name = g_strdup ("XDM-AUTHORIZATION-1");
    }
    else
        authorization_name = g_strdup ("MIT-MAGIC-COOKIE-1");

    /* Check if they support our authorization */
    for (j = packet->Request.authorization_names; *j; j++)
    {
        if (strcmp (*j, authorization_name) == 0)
        {
             match_authorization = TRUE;
             break;
        }
    }

    /* Decline if don't support out authorization */
    if (!match_authorization)
    {
        response = xdmcp_packet_alloc (XDMCP_Decline);
        response->Decline.status = g_strdup_printf ("Server requires %s authorization", authorization_name);
        g_free (authorization_name);
        response->Decline.authentication_name = g_strdup (packet->Request.authentication_name);
        response->Decline.authentication_data.data = authentication_data;
        response->Decline.authentication_data.length = authentication_data_length;
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
        return;
    }

    /* Perform requested authorization */
    if (server->priv->key)
    {
        gint i;
        guint8 key[8], session_key[8];

        /* Setup key */
        decode_key (server->priv->key, key);

        /* Generate a private session key */
        // FIXME: Pick a good DES key?
        session_key[0] = 0;
        for (i = 1; i < 8; i++)
            session_key[i] = g_random_int () & 0xFF;

        /* Encrypt the session key and send it to the server */
        authorization_data = g_malloc (8);
        authorization_data_length = 8;
        XdmcpWrap (session_key, key, authorization_data, authorization_data_length);

        /* Authorization data is the number received from the client followed by the private session key */
        session_authorization_data = g_malloc (16);
        session_authorization_data_length = 16;
        XdmcpDecrementKey (&rho);
        memcpy (session_authorization_data, rho.data, 8);
        memcpy (session_authorization_data + 8, session_key, 8);
    }
    else
    {
        XAuthority *auth;

        /* Data is the cookie */
        auth = x_authority_new_cookie (XAUTH_FAMILY_WILD, NULL, 0, "");
        authorization_data = x_authority_copy_authorization_data (auth);
        authorization_data_length = x_authority_get_authorization_data_length (auth);
        session_authorization_data = x_authority_copy_authorization_data (auth);
        session_authorization_data_length = x_authority_get_authorization_data_length (auth);

        g_object_unref (auth);
    }

    session = add_session (server);
    session->priv->address = connection_to_address (connection);
    session->priv->display_number = packet->Request.display_number;
    display_number = g_strdup_printf ("%d", packet->Request.display_number);

    /* We need to check if this is the loopback address and set the authority
     * for a local connection if this is so as XCB treats "127.0.0.1" as local
     * always */
    if (g_inet_address_get_is_loopback (session->priv->address))
    {
        gchar hostname[1024];
        gethostname (hostname, 1024);

        session->priv->authority = x_authority_new (XAUTH_FAMILY_LOCAL,
                                                    (guint8 *) hostname,
                                                    strlen (hostname),
                                                    display_number,
                                                    authorization_name,
                                                    session_authorization_data,
                                                    session_authorization_data_length);
    }
    else
        session->priv->authority = x_authority_new (connection->type,
                                                    connection->address.data,
                                                    connection->address.length,
                                                    display_number,
                                                    authorization_name,
                                                    session_authorization_data,
                                                    session_authorization_data_length);
    g_free (display_number);

    response = xdmcp_packet_alloc (XDMCP_Accept);
    response->Accept.session_id = xdmcp_session_get_id (session);
    response->Accept.authentication_name = g_strdup (packet->Request.authentication_name);
    response->Accept.authentication_data.data = authentication_data;
    response->Accept.authentication_data.length = authentication_data_length;
    response->Accept.authorization_name = authorization_name;
    response->Accept.authorization_data.data = authorization_data;
    response->Accept.authorization_data.length = authorization_data_length;
    send_packet (socket, address, response);
    xdmcp_packet_free (response);
}

static void
handle_manage (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    XDMCPSession *session;
    gboolean result;

    session = get_session (server, packet->Manage.session_id);
    if (!session)
    {
        XDMCPPacket *response;

        response = xdmcp_packet_alloc (XDMCP_Refuse);
        response->Refuse.session_id = packet->Manage.session_id;
        send_packet (socket, address, response);
        xdmcp_packet_free (response);

        return;
    }

    /* Ignore duplicate requests */
    if (session->priv->started)
    {
        if (session->priv->display_number != packet->Manage.display_number ||
            strcmp (session->priv->display_class, packet->Manage.display_class) != 0)
            g_debug ("Ignoring duplicate Manage with different data");
        return;
    }

    /* Reject if has changed display number */
    if (packet->Manage.display_number != session->priv->display_number)
    {
        XDMCPPacket *response;

        g_debug ("Received Manage for display number %d, but Request was %d", packet->Manage.display_number, session->priv->display_number);
        response = xdmcp_packet_alloc (XDMCP_Refuse);
        response->Refuse.session_id = packet->Manage.session_id;
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
    }

    session->priv->display_class = g_strdup (packet->Manage.display_class);

    g_signal_emit (server, signals[NEW_SESSION], 0, session, &result);
    if (result)
    {
        /* Cancel the inactive timer */
        if (session->priv->inactive_timeout)
            g_source_remove (session->priv->inactive_timeout);

        session->priv->started = TRUE;
    }
    else
    {
        XDMCPPacket *response;

        response = xdmcp_packet_alloc (XDMCP_Failed);
        response->Failed.session_id = packet->Manage.session_id;
        response->Failed.status = g_strdup_printf ("Failed to connect to display :%d", packet->Manage.display_number);
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
    }
}

static void
handle_keep_alive (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    XDMCPPacket *response;
    XDMCPSession *session;
    gboolean alive = FALSE;

    session = get_session (server, packet->KeepAlive.session_id);
    if (session)
        alive = TRUE; // FIXME: xdmcp_session_get_alive (session);

    response = xdmcp_packet_alloc (XDMCP_Alive);
    response->Alive.session_running = alive;
    response->Alive.session_id = alive ? packet->KeepAlive.session_id : 0;
    send_packet (socket, address, response);
    xdmcp_packet_free (response);
}

static gboolean
read_cb (GSocket *socket, GIOCondition condition, XDMCPServer *server)
{
    GSocketAddress *address;
    gchar data[1024];
    GError *error = NULL;
    gssize n_read;

    n_read = g_socket_receive_from (socket, &address, data, 1024, NULL, &error);
    if (error)
        g_warning ("Failed to read from XDMCP socket: %s", error->message);
    g_clear_error (&error);

    if (n_read > 0)
    {
        XDMCPPacket *packet;

        packet = xdmcp_packet_decode ((guint8 *)data, n_read);
        if (packet)
        {
            g_debug ("Got %s", xdmcp_packet_tostring (packet));

            switch (packet->opcode)
            {
            case XDMCP_BroadcastQuery:
            case XDMCP_Query:
            case XDMCP_IndirectQuery:
                handle_query (server, socket, address, packet);
                break;
            case XDMCP_Request:
                handle_request (server, socket, address, packet);
                break;
            case XDMCP_Manage:
                handle_manage (server, socket, address, packet);
                break;
            case XDMCP_KeepAlive:
                handle_keep_alive (server, socket, address, packet);
                break;
            default:
                g_warning ("Got unexpected XDMCP packet %d", packet->opcode);
                break;
            }

            xdmcp_packet_free (packet);
        }
    }

    return TRUE;
}

static GSocket *
open_udp_socket (GSocketFamily family, guint port, const gchar *listen_address, GError **error)
{
    GSocket *socket;
    GSocketAddress *address;
    gboolean result;

    socket = g_socket_new (family, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, error);
    if (!socket)
        return NULL;

    if (listen_address) 
    {
        GList *addresses;

        addresses = g_resolver_lookup_by_name (g_resolver_get_default (), listen_address, NULL, error);
        if (!addresses)
        {
            g_object_unref (socket);
            return NULL;
        }
        address = g_inet_socket_address_new (addresses->data, port);
        g_resolver_free_addresses (addresses);
    }
    else
        address = g_inet_socket_address_new (g_inet_address_new_any (family), port);
    result = g_socket_bind (socket, address, TRUE, error);
    if (!result)
    {
        g_object_unref (socket);
        return NULL;
    }

    return socket;
}

gboolean
xdmcp_server_start (XDMCPServer *server)
{
    GSource *source;
    GError *error = NULL;

    g_return_val_if_fail (server != NULL, FALSE);

    server->priv->socket = open_udp_socket (G_SOCKET_FAMILY_IPV4, server->priv->port, server->priv->listen_address, &error);
    if (error)
        g_warning ("Failed to create IPv4 XDMCP socket: %s", error->message);
    g_clear_error (&error);

    if (server->priv->socket)
    {
        source = g_socket_create_source (server->priv->socket, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }

    server->priv->socket6 = open_udp_socket (G_SOCKET_FAMILY_IPV6, server->priv->port, server->priv->listen_address, &error);
    if (error)
        g_warning ("Failed to create IPv6 XDMCP socket: %s", error->message);
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
xdmcp_server_init (XDMCPServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XDMCP_SERVER_TYPE, XDMCPServerPrivate);

    server->priv->port = XDM_UDP_PORT;
    server->priv->hostname = g_strdup ("");
    server->priv->status = g_strdup ("");
    server->priv->sessions = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
}

static void
xdmcp_server_finalize (GObject *object)
{
    XDMCPServer *self;

    self = XDMCP_SERVER (object);

    if (self->priv->socket)
        g_object_unref (self->priv->socket);
    if (self->priv->socket6)
        g_object_unref (self->priv->socket6);
    g_free (self->priv->listen_address);
    g_free (self->priv->hostname);
    g_free (self->priv->status);
    g_free (self->priv->key);
    g_hash_table_unref (self->priv->sessions);

    G_OBJECT_CLASS (xdmcp_server_parent_class)->finalize (object);
}

static void
xdmcp_server_class_init (XDMCPServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = xdmcp_server_finalize;

    g_type_class_add_private (klass, sizeof (XDMCPServerPrivate));

    signals[NEW_SESSION] =
        g_signal_new (XDMCP_SERVER_SIGNAL_NEW_SESSION,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XDMCPServerClass, new_session),
                      g_signal_accumulator_true_handled,
                      NULL,
                      NULL,
                      G_TYPE_BOOLEAN, 1, XDMCP_SESSION_TYPE);
}
