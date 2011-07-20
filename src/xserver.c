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

#include "xserver.h"
#include "configuration.h"
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
    server->priv->authority = g_object_ref (authority);
}

XAuthority *
xserver_get_authority (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->authority;
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

    G_OBJECT_CLASS (xserver_parent_class)->finalize (object);
}

static void
xserver_class_init (XServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = xserver_finalize;

    g_type_class_add_private (klass, sizeof (XServerPrivate));
}
