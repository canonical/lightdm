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

#ifndef VNC_SERVER_H_
#define VNC_SERVER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define VNC_SERVER_TYPE (vnc_server_get_type())
#define VNC_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), VNC_SERVER_TYPE, VNCServer));

#define VNC_SERVER_SIGNAL_NEW_CONNECTION "new-connection"

typedef struct
{
    GObject parent_instance;
} VNCServer;

typedef struct
{
    GObjectClass parent_class;

    gboolean (*new_connection)(VNCServer *server, GSocket *socket);
} VNCServerClass;

GType vnc_server_get_type (void);

VNCServer *vnc_server_new (void);

void vnc_server_set_port (VNCServer *server, guint port);

guint vnc_server_get_port (VNCServer *server);

void vnc_server_set_listen_address (VNCServer *server, const gchar *listen_address);

const gchar *vnc_server_get_listen_address (VNCServer *server);

gboolean vnc_server_start (VNCServer *server);

G_END_DECLS

#endif /* VNC_SERVER_H_ */
