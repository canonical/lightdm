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

#include "xserver-remote.h"

struct XServerRemotePrivate
{
};

G_DEFINE_TYPE (XServerRemote, xserver_remote, XSERVER_TYPE);

XServerRemote *
xserver_remote_new (const gchar *hostname, guint number)
{
    XServerRemote *self = g_object_new (XSERVER_REMOTE_TYPE, NULL);

    xserver_set_hostname (XSERVER (self), hostname);
    xserver_set_display_number (XSERVER (self), number);

    return self;
}

static gboolean
xserver_remote_start (XServer *server)
{
    return xserver_connect (server);
}

static void
xserver_remote_stop (XServer *server)
{
    xserver_disconnect (server);
}

static void
xserver_remote_init (XServerRemote *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_REMOTE_TYPE, XServerRemotePrivate);
}

static void
xserver_remote_class_init (XServerRemoteClass *klass)
{
    XServerClass *xserver_class = XSERVER_CLASS (klass);

    xserver_class->start = xserver_remote_start;
    xserver_class->stop = xserver_remote_stop;

    g_type_class_add_private (klass, sizeof (XServerRemotePrivate));
}
