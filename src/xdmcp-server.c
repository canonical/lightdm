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
#include "ldm-marshal.h"

#include "xdmcp-server.h"
#include "xdmcp-protocol.h"
#include "xdmcp-session-private.h"
#include "xauthority.h"

enum {
    NEW_SESSION,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct XDMCPServerPrivate
{
    /* Port to listen on */
    guint port;

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

    memset (data, 0, sizeof (data));
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

static void
handle_request (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    int i;
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
    guint16 family;
    GInetAddress *xserver_address = NULL;
    gchar *display_number;
    XdmAuthKeyRec rho;

    /* Try and find an IPv6 address */
    for (i = 0; i < packet->Request.n_connections; i++)
    {
        XDMCPConnection *connection = &packet->Request.connections[i];
        if (connection->type == XAUTH_FAMILY_INTERNET6 && connection->address.length == 16)
        {
            family = connection->type;
            xserver_address = g_inet_address_new_from_bytes (connection->address.data, G_SOCKET_FAMILY_IPV6);

            /* We can't use link-local addresses, as we need to know what interface it is on */
            if (g_inet_address_get_is_link_local (xserver_address))
            {
                g_object_unref (xserver_address);
                xserver_address = NULL;
            }
            else
                break;
        }
    }

    /* If no IPv6 address, then try and find an IPv4 one */
    if (!xserver_address)
    {
        for (i = 0; i < packet->Request.n_connections; i++)
        {
            XDMCPConnection *connection = &packet->Request.connections[i];
            if (connection->type == XAUTH_FAMILY_INTERNET && connection->address.length == 4)
            {
                family = connection->type;
                xserver_address = g_inet_address_new_from_bytes (connection->address.data, G_SOCKET_FAMILY_IPV4);
                break;
            }
        }
    }

    /* Decline if haven't got an address we can connect on */
    if (!xserver_address)
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
        auth = xauth_new_cookie (XAUTH_FAMILY_WILD, NULL, 0, "");
        authorization_data = xauth_copy_authorization_data (auth);
        authorization_data_length = xauth_get_authorization_data_length (auth);
        session_authorization_data = xauth_copy_authorization_data (auth);
        session_authorization_data_length = xauth_get_authorization_data_length (auth);

        g_object_unref (auth);
    }

    session = add_session (server);
    session->priv->address = xserver_address;
    session->priv->display_number = packet->Request.display_number;
    display_number = g_strdup_printf ("%d", packet->Request.display_number);

    /* We need to check if this is the loopback address and set the authority
     * for a local connection if this is so as XCB treats "127.0.0.1" as local
     * always */
    if (g_inet_address_get_is_loopback (xserver_address))
    {
        gchar hostname[1024];
        gethostname (hostname, 1024);

        session->priv->authority = xauth_new (XAUTH_FAMILY_LOCAL,
                                              (guint8 *) hostname,
                                              strlen (hostname),
                                              display_number,
                                              authorization_name,
                                              session_authorization_data,
                                              session_authorization_data_length);
    }
    else
        session->priv->authority = xauth_new (family,
                                              g_inet_address_to_bytes (G_INET_ADDRESS (xserver_address)),
                                              g_inet_address_get_native_size (G_INET_ADDRESS (xserver_address)),
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
open_udp_socket (GSocketFamily family, guint port, GError **error)
{
    GSocket *socket;
    GSocketAddress *address;
    gboolean result;
  
    socket = g_socket_new (family, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, error);
    if (!socket)
        return NULL;

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
  
    server->priv->socket = open_udp_socket (G_SOCKET_FAMILY_IPV4, server->priv->port, &error);
    if (error)
        g_warning ("Failed to create IPv4 XDMCP socket: %s", error->message);
    g_clear_error (&error);
  
    if (server->priv->socket)
    {
        source = g_socket_create_source (server->priv->socket, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }
    
    server->priv->socket6 = open_udp_socket (G_SOCKET_FAMILY_IPV6, server->priv->port, &error);
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
        g_signal_new ("new-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XDMCPServerClass, new_session),
                      g_signal_accumulator_true_handled,
                      NULL,
                      ldm_marshal_BOOLEAN__OBJECT,
                      G_TYPE_BOOLEAN, 1, XDMCP_SESSION_TYPE);
}
