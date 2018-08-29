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

#include "x-server-remote.h"

struct XServerRemotePrivate
{
    /* Display number to use */
    guint display_number;
};

G_DEFINE_TYPE_WITH_PRIVATE (XServerRemote, x_server_remote, X_SERVER_TYPE)

XServerRemote *
x_server_remote_new (const gchar *hostname, guint number, XAuthority *authority)
{
    XServerRemote *server = g_object_new (X_SERVER_REMOTE_TYPE, NULL);

    server->priv->display_number = number;

    x_server_set_hostname (X_SERVER (server), hostname);
    x_server_set_authority (X_SERVER (server), authority);

    return server;
}

static guint
x_server_remote_get_display_number (XServer *server)
{
    return X_SERVER_REMOTE (server)->priv->display_number;
}

static void
x_server_remote_init (XServerRemote *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, X_SERVER_REMOTE_TYPE, XServerRemotePrivate);
}

static void
x_server_remote_class_init (XServerRemoteClass *klass)
{
    XServerClass *x_server_class = X_SERVER_CLASS (klass);  

    x_server_class->get_display_number = x_server_remote_get_display_number;
}
