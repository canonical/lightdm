/*
 * Copyright (C) 2010 Robert Ancell.
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
#include "xauth.h"

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

    /* Auhentication scheme to use */
    gchar *authentication_name;

    /* Auhentication data */  
    guchar *authentication_data;
    gsize authentication_data_length;

    /* Authorization scheme to use */
    gchar *authorization_name;

    /* Authorization data */
    guchar *authorization_data;
    gsize authorization_data_length;

    /* Active XDMCP sessions */
    GHashTable *sessions;
};

G_DEFINE_TYPE (XDMCPServer, xdmcp_server, G_TYPE_OBJECT);

XDMCPServer *
xdmcp_server_new (void)
{
    return g_object_new (XDMCP_SERVER_TYPE, NULL);
}

void
xdmcp_server_set_port (XDMCPServer *server, guint port)
{
    server->priv->port = port;
}

guint
xdmcp_server_get_port (XDMCPServer *server)
{
    return server->priv->port;
}

void
xdmcp_server_set_hostname (XDMCPServer *server, const gchar *hostname)
{
    g_free (server->priv->hostname);
    server->priv->hostname = g_strdup (hostname);
}

const gchar *
xdmcp_server_get_hostname (XDMCPServer *server)
{
    return server->priv->hostname;
}

void
xdmcp_server_set_status (XDMCPServer *server, const gchar *status)
{
    g_free (server->priv->status);
    server->priv->status = g_strdup (status);
}

const gchar *
xdmcp_server_get_status (XDMCPServer *server)
{
    return server->priv->status;
}

void
xdmcp_server_set_authentication (XDMCPServer *server, const gchar *name, const guchar *data, gsize data_length)
{
    g_free (server->priv->authentication_name);
    server->priv->authentication_name = g_strdup (name);
    g_free (server->priv->authentication_data);
    server->priv->authentication_data = g_malloc (data_length);
    server->priv->authentication_data_length = data_length;
    memcpy (server->priv->authentication_data, data, data_length);
}

const gchar *
xdmcp_server_get_authentication_name (XDMCPServer *server)
{
    return server->priv->authentication_name;
}

const guchar *
xdmcp_server_get_authentication_data (XDMCPServer *server)
{
    return server->priv->authentication_data;
}

gsize
xdmcp_server_get_authentication_data_length (XDMCPServer *server)
{
    return server->priv->authentication_data_length;
}

void
xdmcp_server_set_authorization (XDMCPServer *server, const gchar *name, const guchar *data, gsize data_length)
{
    g_free (server->priv->authorization_name);
    server->priv->authorization_name = g_strdup (name);
    g_free (server->priv->authorization_data);
    server->priv->authorization_data = g_malloc (data_length);
    server->priv->authorization_data_length = data_length;
    memcpy (server->priv->authorization_data, data, data_length);
}

const gchar *
xdmcp_server_get_authorization_name (XDMCPServer *server)
{
    return server->priv->authorization_name;
}

const guchar *
xdmcp_server_get_authorization_data (XDMCPServer *server)
{
    return server->priv->authorization_data;
}

gsize
xdmcp_server_get_authorization_data_length (XDMCPServer *server)
{
    return server->priv->authorization_data_length;
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
    session->priv->inactive_timeout = g_timeout_add (10, (GSourceFunc) session_timeout_cb, session);

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
    guchar data[1024];
    gssize n_written;

    g_debug ("Send %s", xdmcp_packet_tostring (packet));
              
    n_written = xdmcp_packet_encode (packet, data, 1024);
    if (n_written < 0)
      g_critical ("Failed to encode XDMCP packet");
    else
    {
        GError *error = NULL;

        if (g_socket_send_to (socket, address, (gchar *) data, n_written, NULL, &error) < 0)
            g_warning ("Error sending packet: %s", error->message);

        g_clear_error (&error);
    }
}

static void
handle_query (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    XDMCPPacket *response;
    gchar **i;
    gboolean match_authentication = FALSE;

    /* If no authentication requested and we are configured for none then allow */
    if (packet->Query.authentication_names[0] == NULL && strcmp (server->priv->authentication_name, "") == 0)
        match_authentication = TRUE;

    for (i = packet->Query.authentication_names; *i; i++)
    {
        if (strcmp (*i, server->priv->authentication_name) == 0)
        {
            match_authentication = TRUE;
            break;
        }
    }

    if (match_authentication)
    {
        response = xdmcp_packet_alloc (XDMCP_Willing);
        response->Willing.authentication_name = g_strdup (server->priv->authentication_name);
        response->Willing.hostname = g_strdup (server->priv->hostname);
        response->Willing.status = g_strdup (server->priv->status);
    }
    else
    {
        response = xdmcp_packet_alloc (XDMCP_Unwilling);
        response->Willing.hostname = g_strdup (server->priv->hostname);
        response->Willing.status = g_strdup (server->priv->status);
    }
  
    send_packet (socket, address, response);

    xdmcp_packet_free (response);
}

static void
handle_request (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    int i;
    XDMCPPacket *response;
    XDMCPSession *session;
    guchar *authentication_data = NULL;
    gsize authentication_data_length = 0;
    gboolean match_authorization = FALSE;
    guchar *authorization_data = NULL;
    gsize authorization_data_length = 0;
    guchar *session_authorization_data = NULL;
    gsize session_authorization_data_length = 0;
    gchar **j;
    GInetAddress *address4 = NULL; /*, *address6 = NULL;*/
    XdmAuthKeyRec rho;
  
    /* Must be using our authentication scheme */
    if (strcmp (packet->Request.authentication_name, server->priv->authentication_name) != 0)
    {
        response = xdmcp_packet_alloc (XDMCP_Decline);
        if (strcmp (server->priv->authentication_name, "") == 0)
            response->Decline.status = g_strdup ("Server does not support authentication");
        else
            response->Decline.status = g_strdup_printf ("Server only supports %s authentication", server->priv->authentication_name);
        response->Decline.authentication_name = g_strdup ("");
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
        return;
    }

    /* Perform requested authentication */
    if (strcmp (server->priv->authentication_name, "XDM-AUTHENTICATION-1") == 0)
    {
        guchar input[8], key[8];

        memset (input, 0, 8);
        memcpy (input, packet->Request.authentication_data.data, packet->Request.authentication_data.length > 8 ? 8 : packet->Request.authentication_data.length);

        /* Setup key */
        memset (key, 0, 8);
        memcpy (key, server->priv->authentication_data, server->priv->authentication_data_length > 8 ? 8 : server->priv->authentication_data_length);

        /* Decode message from server */
        authentication_data = g_malloc (sizeof (guchar) * 8);
        authentication_data_length = 8;

        XdmcpUnwrap (input, key, rho.data, authentication_data_length);
        XdmcpIncrementKey (&rho);
        XdmcpWrap (rho.data, key, authentication_data, authentication_data_length);
    }

    /* Check if they support our authorization */
    for (j = packet->Request.authorization_names; *j; j++)
    {
        if (strcmp (*j, server->priv->authorization_name) == 0)
        {
             match_authorization = TRUE;
             break;
        }
    }
  
    if (!match_authorization)
    {
        response = xdmcp_packet_alloc (XDMCP_Decline);
        if (strcmp (server->priv->authorization_name, "") == 0)
            response->Decline.status = g_strdup ("Server does not support authorization");
        else
            response->Decline.status = g_strdup_printf ("Server only supports %s authorization", server->priv->authorization_name);
        response->Decline.authentication_name = g_strdup (packet->Request.authentication_name);
        response->Decline.authentication_data.data = authentication_data;
        response->Decline.authentication_data.length = authentication_data_length;
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
        return;
    }

    /* Perform requested authorization */
    if (strcmp (server->priv->authorization_name, "MIT-MAGIC-COOKIE-1") == 0)
    {
        /* Data is the cookie */
        authorization_data = g_malloc (sizeof (guchar) * server->priv->authorization_data_length);
        memcpy (authorization_data, server->priv->authorization_data, server->priv->authorization_data_length);
        authorization_data_length = server->priv->authorization_data_length;
    }
    else if (strcmp (server->priv->authorization_name, "XDM-AUTHORIZATION-1") == 0)
    {
        gint i;
        guchar key[8], session_key[8];

        /* Setup key */
        memset (key, 0, 8);
        memcpy (key, server->priv->authentication_data, server->priv->authentication_data_length > 8 ? 8 : server->priv->authentication_data_length);

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

    for (i = 0; i < packet->Request.n_connections; i++)
    {
        XDMCPConnection *connection;

        connection = &packet->Request.connections[i];
        switch (connection->type)
        {
        case FamilyInternet:
            if (connection->address.length == 4)
                address4 = g_inet_address_new_from_bytes (connection->address.data, G_SOCKET_FAMILY_IPV4);
            break;
        /*case FamilyInternet6:
            if (connection->address.length == 16)
                address6 = g_inet_address_new_from_bytes (connection->address.data, G_SOCKET_FAMILY_IPV6);          
            break;*/
        }
    }

    if (!address4)
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

    session = add_session (server);
    session->priv->address = address4; /*address6 ? address6 : address4;*/
    session->priv->authorization_name = g_strdup (server->priv->authorization_name);
    session->priv->authorization_data = session_authorization_data;
    session->priv->authorization_data_length = session_authorization_data_length;

    response = xdmcp_packet_alloc (XDMCP_Accept);
    response->Accept.session_id = xdmcp_session_get_id (session);
    response->Accept.authentication_name = g_strdup (packet->Request.authentication_name);
    response->Accept.authentication_data.data = authentication_data;
    response->Accept.authentication_data.length = authentication_data_length;
    response->Accept.authorization_name = g_strdup (server->priv->authorization_name);
    response->Accept.authorization_data.data = authorization_data;
    response->Accept.authorization_data.length = authorization_data_length;
    send_packet (socket, address, response);
    xdmcp_packet_free (response);
}

static void
handle_manage (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    XDMCPSession *session;

    session = get_session (server, packet->Manage.session_id);
    if (session)
    {
        gchar *display_address;
        gboolean result;

        /* Ignore duplicate requests */
        if (session->priv->started)
        {
            if (session->priv->display_number != packet->Manage.display_number ||
                strcmp (session->priv->display_class, packet->Manage.display_class) != 0)
                g_warning ("Duplicate Manage received with different data");
            return;
        }

        session->priv->display_number = packet->Manage.display_number;  
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
            response->Failed.status = g_strdup_printf ("Failed to connect to display %s", display_address);
            send_packet (socket, address, response);
            xdmcp_packet_free (response);
        }

        g_free (display_address);
    }
    else
    {
        XDMCPPacket *response;

        response = xdmcp_packet_alloc (XDMCP_Refuse);
        response->Refuse.session_id = packet->Manage.session_id;
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
        alive = TRUE; //xdmcp_session_get_alive (session);

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
    if (n_read > 0)
    {
        XDMCPPacket *packet;

        packet = xdmcp_packet_decode ((guchar *)data, n_read);
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
    else
        g_warning ("Failed to read from XDMCP socket: %s", error->message);

    g_clear_error (&error);

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
  
    server->priv->socket = open_udp_socket (G_SOCKET_FAMILY_IPV4, server->priv->port, &error);
    if (server->priv->socket)
    {
        source = g_socket_create_source (server->priv->socket, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }
    else
        g_warning ("Failed to create IPv4 XDMCP socket: %s", error->message);
    g_clear_error (&error);
    server->priv->socket6 = open_udp_socket (G_SOCKET_FAMILY_IPV6, server->priv->port, &error);  
    if (server->priv->socket6)
    {
        source = g_socket_create_source (server->priv->socket6, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }
    else
        g_warning ("Failed to create IPv6 XDMCP socket: %s", error->message);
    g_clear_error (&error);

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
    server->priv->authentication_name = g_strdup ("");
    server->priv->authorization_name = g_strdup ("");
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
    g_free (self->priv->authentication_name);
    g_free (self->priv->authentication_data);
    g_free (self->priv->authorization_name);
    g_free (self->priv->authorization_data);
    g_hash_table_unref (self->priv->sessions);
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
