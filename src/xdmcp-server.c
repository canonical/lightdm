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

#include <X11/X.h>
#include <string.h>
#include <gio/gio.h>

#include "xdmcp-server.h"
#include "xdmcp-protocol.h"
#include "xdmcp-session-private.h"

enum {
    PROP_0,
    PROP_CONFIG,
    PROP_HOSTNAME,
    PROP_STATUS
};

enum {
    SESSION_ADDED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct XDMCPServerPrivate
{
    GKeyFile *config;

    GSocket *socket;

    gchar *hostname;

    gchar *status;

    GHashTable *sessions;
};

G_DEFINE_TYPE (XDMCPServer, xdmcp_server, G_TYPE_OBJECT);

XDMCPServer *
xdmcp_server_new (GKeyFile *config)
{
    return g_object_new (XDMCP_SERVER_TYPE, "config", config, NULL);
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

static XDMCPSession *
add_session (XDMCPServer *server)
{
    XDMCPSession *session;
    guint16 id;

    do
    {
        id = g_random_int ();
    } while (g_hash_table_lookup (server->priv->sessions, GINT_TO_POINTER (id)));

    session = xdmcp_session_new (id);
    g_hash_table_insert (server->priv->sessions, GINT_TO_POINTER (id), session);

    return session;
}

static XDMCPSession *
get_session (XDMCPServer *server, guint16 id)
{
    return g_hash_table_lookup (server->priv->sessions, GINT_TO_POINTER (id));
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
    gchar *authentication_name;

    /* FIXME: Check offered authentication schemes */
    authentication_name = g_strdup ("");

    response = xdmcp_packet_alloc (XDMCP_Willing);
    response->Willing.authentication_name = authentication_name;
    response->Willing.hostname = g_strdup (server->priv->hostname);
    response->Willing.status = g_strdup (server->priv->status);

    send_packet (socket, address, response);

    xdmcp_packet_free (response);
}

static void
handle_request (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    int i;
    XDMCPPacket *response;
    XDMCPSession *session;
    gchar *authentication_name;
    gchar *authorization_name;
    GInetAddress *address4 = NULL; /*, *address6 = NULL;*/

    /* FIXME: Perform requested authentication */
    authentication_name = g_strdup ("");

    /* FIXME: Choose an authorization from the list */
    authorization_name = g_strdup ("");

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

    // FIXME
    //if (!address4 && !address6)
    //    ;

    /* FIXME: Allow a higher layer to decline */

    session = add_session (server);
    session->priv->address = address4; /*address6 ? address6 : address4;*/
    // FIXME: Timeout inactive sessions?

    response = xdmcp_packet_alloc (XDMCP_Accept);
    response->Accept.session_id = xdmcp_session_get_id (session);
    response->Accept.authentication_name = authentication_name;
    response->Accept.authorization_name = authorization_name;
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
        xdmcp_session_manage (session, packet->Manage.display_number, packet->Manage.display_class);

        //FIXME: Only call once
        g_signal_emit (server, signals[SESSION_ADDED], 0, session);
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

gboolean
xdmcp_server_start (XDMCPServer *server)
{
    GSocketAddress *address;
    gint port;
    GSource *source;
    gboolean result;
    GError *error = NULL;
  
    server->priv->socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &error);
    if (!server->priv->socket)
        g_warning ("Failed to create XDMCP socket: %s", error->message);
    g_clear_error (&error);
    if (!server->priv->socket)
        return FALSE;
  
    port = g_key_file_get_integer (server->priv->config, "xdmcp", "port", NULL);
    if (port <= 0)
        port = 177;

    address = g_inet_socket_address_new (g_inet_address_new_any (G_SOCKET_FAMILY_IPV4), port);
    result = g_socket_bind (server->priv->socket, address, TRUE, &error);
    if (!result)
        g_warning ("Failed to bind XDMCP server port: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    source = g_socket_create_source (server->priv->socket, G_IO_IN, NULL);
    g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
    g_source_attach (source, NULL);

    return TRUE;
}

static void
xdmcp_server_init (XDMCPServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XDMCP_SERVER_TYPE, XDMCPServerPrivate);

    server->priv->hostname = g_strdup ("");
    server->priv->status = g_strdup ("");
    server->priv->sessions = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
}

static void
xdmcp_server_set_property(GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    XDMCPServer *self;

    self = XDMCP_SERVER (object);

    switch (prop_id) {
    case PROP_CONFIG:
        self->priv->config = g_value_get_pointer (value);
        break;
    case PROP_HOSTNAME:
        xdmcp_server_set_hostname (self, g_value_get_string (value));
        break;
    case PROP_STATUS:
        xdmcp_server_set_status (self, g_value_get_string (value));      
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}


static void
xdmcp_server_get_property(GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    XDMCPServer *self;

    self = XDMCP_SERVER (object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_pointer (value, self->priv->config);
        break;
    case PROP_HOSTNAME:
        g_value_set_string (value, self->priv->hostname);
        break;
    case PROP_STATUS:
        g_value_set_string (value, self->priv->status);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
xdmcp_server_class_init (XDMCPServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = xdmcp_server_set_property;
    object_class->get_property = xdmcp_server_get_property;

    g_type_class_add_private (klass, sizeof (XDMCPServerPrivate));

    g_object_class_install_property (object_class,
                                     PROP_CONFIG,
                                     g_param_spec_pointer ("config",
                                                           "config",
                                                           "Configuration",
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_HOSTNAME,
                                     g_param_spec_string ("hostname",
                                                          "hostname",
                                                          "Hostname",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     PROP_STATUS,
                                     g_param_spec_string ("status",
                                                          "status",
                                                          "Server status",
                                                          NULL,
                                                          G_PARAM_READWRITE));

    signals[SESSION_ADDED] =
        g_signal_new ("session-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XDMCPServerClass, session_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, XDMCP_SESSION_TYPE);
}
