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

#include "xdmcp-server.h"
#include "xdmcp-protocol.h"
#include "xdmcp-session-private.h"

enum {
    PROP_0,
    PROP_CONFIG,
    PROP_HOSTNAME,
    PROP_STATUS,
    PROP_AUTHENTICATION_KEY
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

    gchar *authentication_key_string;

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

void
xdmcp_server_set_authentication_key (XDMCPServer *server, const gchar *key_string)
{
    g_free (server->priv->authentication_key_string);
    server->priv->authentication_key_string = g_strdup (key_string);
}

const gchar *
xdmcp_server_get_authentication_key (XDMCPServer *server)
{
    return server->priv->authentication_key_string;
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
    const gchar *authentication_name = "";
    gchar **i;

    /* Use authentication if we are configured for it */
    for (i = packet->Query.authentication_names; *i; i++)
    {
        if (strcmp (*i, "XDM-AUTHENTICATION-1") == 0)
        {
            if (server->priv->authentication_key_string)
                authentication_name = "XDM-AUTHENTICATION-1";
        }
    }

    response = xdmcp_packet_alloc (XDMCP_Willing);
    response->Willing.authentication_name = g_strdup (authentication_name);
    response->Willing.hostname = g_strdup (server->priv->hostname);
    response->Willing.status = g_strdup (server->priv->status);

    send_packet (socket, address, response);

    xdmcp_packet_free (response);
}

static guchar
atox (gchar c)
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
string_to_key (XdmAuthKeyRec *key, const gchar *string)
{
    gint i;
  
    if (strncmp (string, "0x", 2) == 0 || strncmp (string, "0X", 2) == 0)
    {
        gint j = 0;

        for (i = 0; i < 8 && string[j]; i++)
        {
            key->data[i] = 0;

            if (string[j])
            {
                key->data[i] |= atox (string[j]) >> 8;
                j++;
                if (string[j])
                {
                    key->data[i] |= atox (string[j+1]);
                    j++;
                }
            }
        }
    }
    else
    {
        key->data[0] = 0;
        for (i = 1; i < 8 && string[i-1]; i++)
            key->data[i] = string[i-1];
        for (; i < 8; i++)
            key->data[i] = 0;
    }
}

static void
handle_request (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    int i;
    XDMCPPacket *response;
    XDMCPSession *session;
    const gchar *authorization_name = "";
    guchar *authentication_data = NULL;
    gsize authentication_data_length = 0;
    gchar **j;
    GInetAddress *address4 = NULL; /*, *address6 = NULL;*/
  
    /* FIXME: If session not started (i.e. not received the Manage then response with Accept again) */

    /* FIXME: Perform requested authentication */
    if (strcmp (packet->Request.authentication_name, "") == 0)
        ; /* No authentication */
    else if (strcmp (packet->Request.authentication_name, "XDM-AUTHENTICATION-1") == 0)
    {
        XdmAuthKeyRec key, message;

        if (!server->priv->authentication_key_string)
        {
            // FIXME: Send Decline
            return;
        }

        // FIXME: I don't think it technically has to be 8 but the Xdmcp library requires it
        if (packet->Request.authentication_data.length != 8)
        {
            // FIXME: Send Decline
            return;
        }

        /* Setup key */
        string_to_key (&key, server->priv->authentication_key_string);

        /* Decode message from server */
        authentication_data = g_malloc (sizeof (guchar) * packet->Request.authentication_data.length);
        authentication_data_length = packet->Request.authentication_data.length;

        XdmcpUnwrap (packet->Request.authentication_data.data, key.data, message.data, authentication_data_length);
        XdmcpIncrementKey (&message);
        XdmcpWrap (message.data, key.data, authentication_data, authentication_data_length);

        //authorization_name = "XDM_AUTHORIZATION-1";
    }
    else
    {
        // FIXME: Send Decline
        return;
    }

    /* Choose an authorization from the list */
    for (j = packet->Request.authorization_names; *j; j++)
    {
        if (strcmp (*j, "MIT-MAGIC-COOKIE-1") == 0)
        {
            // FIXME: Generate cookie
        }
        else if (strcmp (*j, "XDM-AUTHORIZATION-1") == 0)
        {
        }
    }

    // FIXME: Check have supported authorization

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
        response->Accept.authentication_data.data = authentication_data;
        response->Accept.authentication_data.length = authentication_data_length;
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
        return;
    }

    /* FIXME: Allow a higher layer to decline */

    session = add_session (server);
    session->priv->address = address4; /*address6 ? address6 : address4;*/
    // FIXME: Timeout inactive sessions?

    response = xdmcp_packet_alloc (XDMCP_Accept);
    response->Accept.session_id = xdmcp_session_get_id (session);
    response->Accept.authentication_name = g_strdup (packet->Request.authentication_name);
    response->Accept.authentication_data.data = authentication_data;
    response->Accept.authentication_data.length = authentication_data_length;
    response->Accept.authorization_name = g_strdup (authorization_name);
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
        gchar *ip_address, *display_address;

        /* Ignore duplicate requests */
        if (session->priv->started)
        {
            if (session->priv->display_number != packet->Manage.display_number ||
                strcmp (session->priv->display_class, packet->Manage.display_class) != 0)
                g_warning ("Duplicate Manage received with different data");
            return;
        }

        /* Try and connect */
        ip_address = g_inet_address_to_string (G_INET_ADDRESS (session->priv->address));
        display_address = g_strdup_printf ("%s:%d", ip_address, packet->Manage.display_number);
        g_free (ip_address);
        session->priv->connection = xcb_connect (display_address, NULL);
        // TODO: xcb_connect_to_display_with_auth_info (display_address, NULL);
      
        if (!session->priv->connection)
        {
            XDMCPPacket *response;

            response = xdmcp_packet_alloc (XDMCP_Failed);
            response->Failed.session_id = packet->Manage.session_id;
            response->Failed.status = g_strdup_printf ("Failed to connect to display %s", display_address);
            send_packet (socket, address, response);
            xdmcp_packet_free (response);
        }
        else
        {
            session->priv->started = TRUE;
            session->priv->display_number = packet->Manage.display_number;  
            session->priv->display_class = g_strdup (packet->Manage.display_class);
            g_signal_emit (server, signals[SESSION_ADDED], 0, session);
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
    case PROP_AUTHENTICATION_KEY:
        xdmcp_server_set_authentication_key (self, g_value_get_string (value));      
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
    case PROP_AUTHENTICATION_KEY:
        g_value_set_string (value, xdmcp_server_get_authentication_key (self));
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
    g_object_class_install_property (object_class,
                                     PROP_AUTHENTICATION_KEY,
                                     g_param_spec_string ("authentication-key",
                                                          "authentication-key",
                                                          "Authentication key",
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
