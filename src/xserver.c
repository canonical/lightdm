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

#include <config.h>
#include <string.h>
#include <xcb/xcb.h>

#include "xserver.h"
#include "xsession.h"

struct XServerPrivate
{  
    /* Host running the server */
    gchar *hostname;

    /* Display number */
    guint number;

    /* Cached server address */
    gchar *address;

    /* Authority */
    XAuthority *authority;

    /* Connection to this X server */
    xcb_connection_t *connection;
};

G_DEFINE_TYPE (XServer, xserver, DISPLAY_SERVER_TYPE);

void
xserver_set_hostname (XServer *server, const gchar *hostname)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->hostname);
    server->priv->hostname = g_strdup (hostname);
    g_free (server->priv->address);
    server->priv->address = NULL;
}

gchar *
xserver_get_hostname (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->hostname;
}

void
xserver_set_display_number (XServer *server, guint number)
{
    g_return_if_fail (server != NULL);
    server->priv->number = number;
    g_free (server->priv->address);
    server->priv->address = NULL;
}

guint
xserver_get_display_number (XServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->number;
}

const gchar *
xserver_get_address (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);

    if (!server->priv->address)
    {
        if (server->priv->hostname)
            server->priv->address = g_strdup_printf("%s:%d", server->priv->hostname, server->priv->number);
        else
            server->priv->address = g_strdup_printf(":%d", server->priv->number);
    }  

    return server->priv->address;
}

void
xserver_set_authority (XServer *server, XAuthority *authority)
{
    g_return_if_fail (server != NULL);

    if (server->priv->authority)
        g_object_unref (server->priv->authority);
    if (authority)
        server->priv->authority = g_object_ref (authority);
    else
        server->priv->authority = NULL;
}

XAuthority *
xserver_get_authority (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->authority;
}

static gboolean
xserver_start (DisplayServer *display_server)
{
    XServer *server = XSERVER (display_server);
    xcb_auth_info_t *auth = NULL, a;

    if (server->priv->authority)
    {
        a.namelen = strlen (xauth_get_authorization_name (server->priv->authority));
        a.name = (char *) xauth_get_authorization_name (server->priv->authority);
        a.datalen = xauth_get_authorization_data_length (server->priv->authority);
        a.data = (char *) xauth_get_authorization_data (server->priv->authority);
        auth = &a;
    }

    /* Open connection */  
    g_debug ("Connecting to XServer %s", xserver_get_address (server));
    server->priv->connection = xcb_connect_to_display_with_auth_info (xserver_get_address (server), auth, NULL);
    if (xcb_connection_has_error (server->priv->connection))
    {
        g_debug ("Error connecting to XServer %s", xserver_get_address (server));
        return FALSE;
    }

    return DISPLAY_SERVER_CLASS (xserver_parent_class)->start (display_server);
}

static void
xserver_init (XServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_TYPE, XServerPrivate);
}

static void
xserver_finalize (GObject *object)
{
    XServer *self;

    self = XSERVER (object);

    g_free (self->priv->hostname);
    g_free (self->priv->address);
    if (self->priv->authority)
        g_object_unref (self->priv->authority);
    if (self->priv->connection)
        xcb_disconnect (self->priv->connection);

    G_OBJECT_CLASS (xserver_parent_class)->finalize (object);
}

static void
xserver_class_init (XServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->start = xserver_start;
    object_class->finalize = xserver_finalize;

    g_type_class_add_private (klass, sizeof (XServerPrivate));
}
