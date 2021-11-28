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
#include "x-authority.h"

enum {
    NEW_SESSION,
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

    /* Hostname to report to client */
    gchar *hostname;

    /* Status to report to clients */
    gchar *status;

    /* XDM-AUTHENTICATION-1 key */
    gchar *key;

    /* Known XDMCP sessions */
    GHashTable *sessions;
} XDMCPServerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (XDMCPServer, xdmcp_server, G_TYPE_OBJECT)

/* Maximum number of milliseconds client will resend manage requests before giving up */
#define MANAGE_TIMEOUT 126000

/* Address sort support structure */
typedef struct
{
    gsize index;
    GInetAddress *address;
} AddrSortItem;

XDMCPServer *xdmcp_server_new(void)
{
    return g_object_new (XDMCP_SERVER_TYPE, NULL);
}

void
xdmcp_server_set_port (XDMCPServer *server, guint port)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    g_return_if_fail (server != NULL);
    priv->port = port;
}

guint
xdmcp_server_get_port (XDMCPServer *server)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    g_return_val_if_fail (server != NULL, 0);
    return priv->port;
}

void
xdmcp_server_set_listen_address (XDMCPServer *server, const gchar *listen_address)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->listen_address);
    priv->listen_address = g_strdup (listen_address);
}

const gchar *
xdmcp_server_get_listen_address (XDMCPServer *server)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    g_return_val_if_fail (server != NULL, NULL);
    return priv->listen_address;
}

void
xdmcp_server_set_hostname (XDMCPServer *server, const gchar *hostname)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->hostname);
    priv->hostname = g_strdup (hostname);
}

const gchar *
xdmcp_server_get_hostname (XDMCPServer *server)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    g_return_val_if_fail (server != NULL, NULL);
    return priv->hostname;
}

void
xdmcp_server_set_status (XDMCPServer *server, const gchar *status)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->status);
    priv->status = g_strdup (status);
}

const gchar *
xdmcp_server_get_status (XDMCPServer *server)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    g_return_val_if_fail (server != NULL, NULL);
    return priv->status;
}

void
xdmcp_server_set_key (XDMCPServer *server, const gchar *key)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->key);
    priv->key = g_strdup (key);
}

typedef struct
{
    XDMCPServer *server;
    XDMCPSession *session;
    guint timeout_source;
} SessionData;

static void
session_data_free (SessionData *data)
{
    g_object_unref (data->session);
    if (data->timeout_source != 0)
        g_source_remove (data->timeout_source);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SessionData, session_data_free)

static gboolean
session_timeout_cb (gpointer user_data)
{
    g_autoptr(SessionData) data = user_data;
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (data->server);

    g_debug ("Timing out unmanaged session %d", xdmcp_session_get_id (data->session));
    g_hash_table_remove (priv->sessions, GINT_TO_POINTER ((gint) xdmcp_session_get_id (data->session)));
    return G_SOURCE_REMOVE;
}

static XDMCPSession *
add_session (XDMCPServer *server, GInetAddress *address, guint16 display_number, XAuthority *authority)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);

    guint16 id;
    do
    {
        id = g_random_int () & 0xFFFFFFFF;
    } while (g_hash_table_lookup (priv->sessions, GINT_TO_POINTER ((gint) id)));

    SessionData *data = g_malloc0 (sizeof (SessionData));
    data->server = server;
    data->session = xdmcp_session_new (id, address, display_number, authority);
    data->timeout_source = g_timeout_add (MANAGE_TIMEOUT, session_timeout_cb, data);
    g_hash_table_insert (priv->sessions, GINT_TO_POINTER ((gint) id), data);

    return data->session;
}

static SessionData *
get_session_data (XDMCPServer *server, guint16 id)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    return g_hash_table_lookup (priv->sessions, GINT_TO_POINTER ((gint) id));
}

static gchar *
socket_address_to_string (GSocketAddress *address)
{
    g_autofree gchar *inet_text = g_inet_address_to_string (g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address)));
    return g_strdup_printf ("%s:%d", inet_text, g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (address)));
}

static void
send_packet (GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    g_autofree gchar *address_string = socket_address_to_string (address);
    g_debug ("Send %s to %s", xdmcp_packet_tostring (packet), address_string);

    guint8 data[1024];
    gssize n_written = xdmcp_packet_encode (packet, data, 1024);
    if (n_written < 0)
        g_critical ("Failed to encode XDMCP packet");
    else
    {
        g_autoptr(GError) error = NULL;
        g_socket_send_to (socket, address, (gchar *) data, n_written, NULL, &error);
        if (error)
            g_warning ("Error sending packet: %s", error->message);
    }
}

static const gchar *
get_authentication_name (XDMCPServer *server)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);
    if (priv->key)
        return "XDM-AUTHENTICATION-1";
    else
        return "";
}

static void
handle_query (XDMCPServer *server, GSocket *socket, GSocketAddress *address, gchar **authentication_names)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);

    /* If no authentication requested and we are configured for none then allow */
    const gchar *authentication_name = NULL;
    if (authentication_names[0] == NULL && priv->key == NULL)
        authentication_name = "";

    for (gchar **i = authentication_names; *i; i++)
    {
        if (strcmp (*i, get_authentication_name (server)) == 0 && priv->key != NULL)
        {
            authentication_name = *i;
            break;
        }
    }

    XDMCPPacket *response;
    if (authentication_name)
    {
        response = xdmcp_packet_alloc (XDMCP_Willing);
        response->Willing.authentication_name = g_strdup (authentication_name);
        response->Willing.hostname = g_strdup (priv->hostname);
        response->Willing.status = g_strdup (priv->status);
    }
    else
    {
        response = xdmcp_packet_alloc (XDMCP_Unwilling);
        response->Unwilling.hostname = g_strdup (priv->hostname);
        if (priv->key)
            response->Unwilling.status = g_strdup_printf ("No matching authentication, server requires %s", get_authentication_name (server));
        else
            response->Unwilling.status = g_strdup ("No matching authentication");
    }

    send_packet (socket, address, response);

    xdmcp_packet_free (response);
}

static void
handle_forward_query (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    GSocketFamily family = g_socket_get_family (socket);
    switch (family)
    {
    case G_SOCKET_FAMILY_IPV4:
        if (packet->ForwardQuery.client_address.length != 4)
        {
            g_warning ("Ignoring IPv4 XDMCP ForwardQuery with client address of length %d", packet->ForwardQuery.client_address.length);
            return;
        }
        break;
    case G_SOCKET_FAMILY_IPV6:
        if (packet->ForwardQuery.client_address.length != 16)
        {
            g_warning ("Ignoring IPv6 XDMCP ForwardQuery with client address of length %d", packet->ForwardQuery.client_address.length);
            return;
        }
        break;
    default:
        g_warning ("Unknown socket family %d", family);
        return;
    }

    guint16 port = 0;
    for (int i = 0; i < packet->ForwardQuery.client_port.length; i++)
        port = port << 8 | packet->ForwardQuery.client_port.data[i];

    g_autoptr(GInetAddress) client_inet_address = g_inet_address_new_from_bytes (packet->ForwardQuery.client_address.data, family);
    g_autoptr(GSocketAddress) client_address = g_inet_socket_address_new (client_inet_address, port);

    handle_query (server, socket, client_address, packet->ForwardQuery.authentication_names);
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
    memset (data, 0, 8);
    if (strncmp (key, "0x", 2) == 0 || strncmp (key, "0X", 2) == 0)
    {
        for (gint i = 0; i < 8; i++)
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
        for (gint i = 1; i < 8 && key[i-1]; i++)
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

/* Sort function to order XDMCP addresses by which is best to connect to */
static gint
compare_addresses (gconstpointer a, gconstpointer b, gpointer user_data)
{
    const AddrSortItem *item_a = a;
    const AddrSortItem *item_b = b;
    GInetAddress *source_address = user_data;

    /* Prefer non link-local addresses */
    gboolean is_link_local = g_inet_address_get_is_link_local (item_a->address);
    if (is_link_local != g_inet_address_get_is_link_local (item_b->address))
        return is_link_local ? 1 : -1;

    /* Prefer the source address family */
    GSocketFamily family_a = g_inet_address_get_family (item_a->address);
    GSocketFamily family_b = g_inet_address_get_family (item_b->address);
    if (family_a != family_b)
    {
        GSocketFamily family = g_inet_address_get_family (source_address);
        if (family_a == family)
            return -1;
        if (family_b == family)
            return 1;
        return family_a < family_b ? -1 : 1;
    }

    /* Check equality */
    if (g_inet_address_equal (item_a->address, item_b->address))
        return 0;

    /* Prefer the source address */
    if (g_inet_address_equal (source_address, item_a->address))
        return -1;
    if (g_inet_address_equal (source_address, item_b->address))
        return 1;

    /* Addresses are not equal, but preferences are: order is undefined */
    return 0;
}

static XDMCPConnection *
choose_connection (XDMCPPacket *packet, GInetAddress *source_address)
{
    gsize addresses_length = packet->Request.n_connections;
    if (addresses_length == 0)
        return NULL;

    AddrSortItem addr;
    GArray *addresses = g_array_sized_new (FALSE, FALSE, sizeof addr, addresses_length);
    if (!addresses)
        return NULL;

    for (gsize i = 0; i < addresses_length; i++)
    {
        addr.address = connection_to_address (&packet->Request.connections[i]);
        if (addr.address)
        {
            addr.index = i;
            g_array_append_val (addresses, addr);
        }
    }

    /* Sort the addresses according to our preferences */
    g_array_sort_with_data (addresses, compare_addresses, source_address);

    /* Use the best address */
    gssize index = -1;
    if (addresses->len)
        index = g_array_index (addresses, AddrSortItem, 0).index;

    /* Free the local sort array and items */
    for (gsize i = 0; i < addresses->len; i++)
        g_object_unref (g_array_index (addresses, AddrSortItem, i).address);
    g_array_unref (addresses);

    return index >= 0 ? &packet->Request.connections[index] : NULL;
}

static gboolean
has_string (gchar **list, const gchar *text)
{
    for (gchar **i = list; *i; i++)
        if (strcmp (*i, text) == 0)
            return TRUE;

    return FALSE;
}

static void
handle_request (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);

    /* Check authentication */
    g_autofree gchar *authentication_name = NULL;
    g_autofree guint8 *authentication_data = NULL;
    gsize authentication_data_length = 0;
    g_autofree gchar *decline_status = NULL;
    XdmAuthKeyRec rho;
    if (strcmp (packet->Request.authentication_name, "") == 0)
    {
        if (!priv->key)
        {
            if (!has_string (packet->Request.authorization_names, "MIT-MAGIC-COOKIE-1"))
                decline_status = g_strdup ("No matching authorization, server requires MIT-MAGIC-COOKIE-1");
        }
        else
            decline_status = g_strdup ("No matching authentication, server requires XDM-AUTHENTICATION-1");
    }
    else if (strcmp (packet->Request.authentication_name, "XDM-AUTHENTICATION-1") == 0 && priv->key)
    {
        if (packet->Request.authentication_data.length == 8)
        {
            guint8 input[8], key[8];

            memcpy (input, packet->Request.authentication_data.data, packet->Request.authentication_data.length);

            /* Setup key */
            decode_key (priv->key, key);

            /* Decode message from server */
            authentication_name = g_strdup ("XDM-AUTHENTICATION-1");
            authentication_data = g_malloc (sizeof (guint8) * 8);
            authentication_data_length = 8;

            XdmcpUnwrap (input, key, rho.data, authentication_data_length);
            XdmcpIncrementKey (&rho);
            XdmcpWrap (rho.data, key, authentication_data, authentication_data_length);

            if (!has_string (packet->Request.authorization_names, "XDM-AUTHORIZATION-1"))
                decline_status = g_strdup ("No matching authorization, server requires XDM-AUTHORIZATION-1");
        }
        else
            decline_status = g_strdup ("Invalid XDM-AUTHENTICATION-1 data provided");
    }
    else
    {
        if (strcmp (packet->Request.authentication_name, "") == 0)
            decline_status = g_strdup_printf ("No matching authentication, server does not support unauthenticated connections");
        else if (priv->key)
            decline_status = g_strdup ("No matching authentication, server requires XDM-AUTHENTICATION-1");
        else
            decline_status = g_strdup ("No matching authentication, server only supports unauthenticated connections");
    }

    /* Choose an address to connect back on */
    XDMCPConnection *connection = choose_connection (packet, g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address)));
    if (!connection && !decline_status)
        decline_status = g_strdup ("No valid address found");

    if (!authentication_name)
        authentication_name = g_strdup ("");

    /* Decline if request was not valid */
    if (decline_status)
    {
        XDMCPPacket *response = xdmcp_packet_alloc (XDMCP_Decline);
        response->Decline.status = g_steal_pointer (&decline_status);
        response->Decline.authentication_name = g_steal_pointer (&authentication_name);
        response->Decline.authentication_data.data = g_steal_pointer (&authentication_data);
        response->Decline.authentication_data.length = authentication_data_length;
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
        return;
    }

    /* Generate authorization data */
    g_autofree gchar *authorization_name = NULL;
    g_autofree guint8 *authorization_data = NULL;
    gsize authorization_data_length = 0;
    g_autofree guint8 *session_authorization_data = NULL;
    gsize session_authorization_data_length = 0;
    if (priv->key)
    {
        /* Setup key */
        guint8 key[8];
        decode_key (priv->key, key);

        /* Generate a private session key */
        // FIXME: Pick a good DES key?
        guint8 session_key[8];
        session_key[0] = 0;
        for (gint i = 1; i < 8; i++)
            session_key[i] = g_random_int () & 0xFF;

        /* Encrypt the session key and send it to the server */
        authorization_data = g_malloc (8);
        authorization_data_length = 8;
        XdmcpWrap (session_key, key, authorization_data, authorization_data_length);

        /* Authorization data is the number received from the client followed by the private session key */
        authorization_name = g_strdup ("XDM-AUTHORIZATION-1");
        session_authorization_data = g_malloc (16);
        session_authorization_data_length = 16;
        XdmcpDecrementKey (&rho);
        memcpy (session_authorization_data, rho.data, 8);
        memcpy (session_authorization_data + 8, session_key, 8);
    }
    else
    {
        /* Data is the cookie */
        g_autoptr(XAuthority) auth = x_authority_new_cookie (XAUTH_FAMILY_WILD, NULL, 0, "");
        authorization_data = x_authority_copy_authorization_data (auth);
        authorization_data_length = x_authority_get_authorization_data_length (auth);
        authorization_name = g_strdup ("MIT-MAGIC-COOKIE-1");
        session_authorization_data = x_authority_copy_authorization_data (auth);
        session_authorization_data_length = x_authority_get_authorization_data_length (auth);
    }

    /* We need to check if this is the loopback address and set the authority
     * for a local connection if this is so as XCB treats "127.0.0.1" as local
     * always */
    g_autoptr(GInetAddress) session_address = connection_to_address (connection);
    g_autofree gchar *display_number = g_strdup_printf ("%d", packet->Request.display_number);
    g_autoptr(XAuthority) authority = NULL;
    if (g_inet_address_get_is_loopback (session_address))
    {
        gchar hostname[1024];
        gethostname (hostname, 1024);

        authority = x_authority_new (XAUTH_FAMILY_LOCAL,
                                     (guint8 *) hostname,
                                     strlen (hostname),
                                     display_number,
                                     authorization_name,
                                     session_authorization_data,
                                     session_authorization_data_length);
    }
    else
        authority = x_authority_new (connection->type,
                                     connection->address.data,
                                     connection->address.length,
                                     display_number,
                                     authorization_name,
                                     session_authorization_data,
                                     session_authorization_data_length);

    XDMCPSession *session = add_session (server, session_address, packet->Request.display_number, authority);

    XDMCPPacket *response = xdmcp_packet_alloc (XDMCP_Accept);
    response->Accept.session_id = xdmcp_session_get_id (session);
    response->Accept.authentication_name = g_steal_pointer (&authentication_name);
    response->Accept.authentication_data.data = g_steal_pointer (&authentication_data);
    response->Accept.authentication_data.length = authentication_data_length;
    response->Accept.authorization_name = g_steal_pointer (&authorization_name);
    response->Accept.authorization_data.data = g_steal_pointer (&authorization_data);
    response->Accept.authorization_data.length = authorization_data_length;
    send_packet (socket, address, response);
    xdmcp_packet_free (response);
}

static void
handle_manage (XDMCPServer *server, GSocket *socket, GSocketAddress *address, XDMCPPacket *packet)
{
    SessionData *data = get_session_data (server, packet->Manage.session_id);
    if (!data->session)
    {
        XDMCPPacket *response = xdmcp_packet_alloc (XDMCP_Refuse);
        response->Refuse.session_id = packet->Manage.session_id;
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
        return;
    }

    /* Ignore duplicate requests */
    if (data->timeout_source == 0)
    {
        if (xdmcp_session_get_display_number (data->session) != packet->Manage.display_number ||
            strcmp (xdmcp_session_get_display_class (data->session), packet->Manage.display_class) != 0)
            g_debug ("Ignoring duplicate Manage with different data");
        return;
    }

    /* Reject if has changed display number */
    if (packet->Manage.display_number != xdmcp_session_get_display_number (data->session))
    {
        XDMCPPacket *response;

        g_debug ("Received Manage for display number %d, but Request was %d", packet->Manage.display_number, xdmcp_session_get_display_number (data->session));
        response = xdmcp_packet_alloc (XDMCP_Refuse);
        response->Refuse.session_id = packet->Manage.session_id;
        send_packet (socket, address, response);
        xdmcp_packet_free (response);
    }

    xdmcp_session_set_display_class (data->session, packet->Manage.display_class);

    gboolean result = FALSE;
    g_signal_emit (server, signals[NEW_SESSION], 0, data->session, &result);
    if (result)
    {
        /* Cancel the inactive timer */
        if (data->timeout_source != 0)
            g_source_remove (data->timeout_source);
        data->timeout_source = 0;
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
    SessionData *data = get_session_data (server, packet->KeepAlive.session_id);
    gboolean alive = FALSE;
    if (data)
        alive = TRUE; // FIXME: xdmcp_session_get_alive (session);

    XDMCPPacket *response = xdmcp_packet_alloc (XDMCP_Alive);
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
    g_autoptr(GError) error = NULL;
    gssize n_read;

    n_read = g_socket_receive_from (socket, &address, data, 1024, NULL, &error);
    if (error)
        g_warning ("Failed to read from XDMCP socket: %s", error->message);

    if (n_read > 0)
    {
        XDMCPPacket *packet;

        packet = xdmcp_packet_decode ((guint8 *)data, n_read);
        if (packet)
        {
            g_autofree gchar *packet_string = NULL;
            g_autofree gchar *address_string = NULL;

            packet_string = xdmcp_packet_tostring (packet);
            address_string = socket_address_to_string (address);
            g_debug ("Got %s from %s", packet_string, address_string);

            switch (packet->opcode)
            {
            case XDMCP_BroadcastQuery:
            case XDMCP_Query:
            case XDMCP_IndirectQuery:
                handle_query (server, socket, address, packet->Query.authentication_names);
                break;
            case XDMCP_ForwardQuery:
                handle_forward_query (server, socket, address, packet);
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
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketAddress) address = NULL;
    gboolean result;

    socket = g_socket_new (family, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, error);
    if (!socket)
        return NULL;

    if (listen_address)
    {
        GList *addresses;

        addresses = g_resolver_lookup_by_name (g_resolver_get_default (), listen_address, NULL, error);
        if (!addresses)
            return NULL;
        address = g_inet_socket_address_new (addresses->data, port);
        g_resolver_free_addresses (addresses);
    }
    else
        address = g_inet_socket_address_new (g_inet_address_new_any (family), port);
    result = g_socket_bind (socket, address, TRUE, error);
    if (!result)
        return NULL;

    return g_steal_pointer (&socket);
}

gboolean
xdmcp_server_start (XDMCPServer *server)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);

    g_return_val_if_fail (server != NULL, FALSE);

    g_autoptr(GError) ipv4_error = NULL;
    priv->socket = open_udp_socket (G_SOCKET_FAMILY_IPV4, priv->port, priv->listen_address, &ipv4_error);
    if (ipv4_error)
        g_warning ("Failed to create IPv4 XDMCP socket: %s", ipv4_error->message);

    if (priv->socket)
    {
        GSource *source = g_socket_create_source (priv->socket, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }

    g_autoptr(GError) ipv6_error = NULL;
    priv->socket6 = open_udp_socket (G_SOCKET_FAMILY_IPV6, priv->port, priv->listen_address, &ipv6_error);
    if (ipv6_error)
        g_warning ("Failed to create IPv6 XDMCP socket: %s", ipv6_error->message);

    if (priv->socket6)
    {
        GSource *source = g_socket_create_source (priv->socket6, G_IO_IN, NULL);
        g_source_set_callback (source, (GSourceFunc) read_cb, server, NULL);
        g_source_attach (source, NULL);
    }

    if (!priv->socket && !priv->socket6)
        return FALSE;

    return TRUE;
}

static void
xdmcp_server_init (XDMCPServer *server)
{
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (server);

    priv->port = XDM_UDP_PORT;
    priv->hostname = g_strdup ("");
    priv->status = g_strdup ("");
    priv->sessions = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) session_data_free);
}

static void
xdmcp_server_finalize (GObject *object)
{
    XDMCPServer *self = XDMCP_SERVER (object);
    XDMCPServerPrivate *priv = xdmcp_server_get_instance_private (self);

    g_clear_object (&priv->socket);
    g_clear_object (&priv->socket6);
    g_clear_pointer (&priv->listen_address, g_free);
    g_clear_pointer (&priv->hostname, g_free);
    g_clear_pointer (&priv->status, g_free);
    g_clear_pointer (&priv->key, g_free);
    g_clear_pointer (&priv->sessions, g_hash_table_unref);

    G_OBJECT_CLASS (xdmcp_server_parent_class)->finalize (object);
}

static void
xdmcp_server_class_init (XDMCPServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = xdmcp_server_finalize;

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
