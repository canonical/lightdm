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

G_DEFINE_TYPE (XServerRemote, x_server_remote, X_SERVER_TYPE);

XServerRemote *
x_server_remote_new (const gchar *hostname, guint number, XAuthority *authority)
{
    XServerRemote *self = g_object_new (X_SERVER_REMOTE_TYPE, NULL);
    gchar *name;

    x_server_set_hostname (X_SERVER (self), hostname);
    x_server_set_display_number (X_SERVER (self), number);
    x_server_set_authority (X_SERVER (self), authority);

    name = g_strdup_printf ("x-%s-%d", hostname, number);
    display_server_set_name (DISPLAY_SERVER (self), name);
    g_free (name);

    return self;
}

static void
x_server_remote_init (XServerRemote *server)
{
}

static void
x_server_remote_class_init (XServerRemoteClass *klass)
{
}
