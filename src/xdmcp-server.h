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

#ifndef _XDMCP_SERVER_H_
#define _XDMCP_SERVER_H_

#include <glib-object.h>

#include "xdmcp-session.h"

G_BEGIN_DECLS

#define XDMCP_SERVER_TYPE (xdmcp_server_get_type())
#define XDMCP_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XDMCP_SERVER_TYPE, XDMCPServer));

typedef struct XDMCPServerPrivate XDMCPServerPrivate;

typedef struct
{
    GObject         parent_instance;
    XDMCPServerPrivate *priv;
} XDMCPServer;

typedef struct
{
    GObjectClass parent_class;

    gboolean (*new_session)(XDMCPServer *server, XDMCPSession *session);
} XDMCPServerClass;

GType xdmcp_server_get_type (void);

XDMCPServer *xdmcp_server_new (void);

void xdmcp_server_set_port (XDMCPServer *server, guint port);

guint xdmcp_server_get_port (XDMCPServer *server);

void xdmcp_server_set_hostname (XDMCPServer *server, const gchar *hostname);

const gchar *xdmcp_server_get_hostname (XDMCPServer *server);

void xdmcp_server_set_status (XDMCPServer *server, const gchar *status);

const gchar *xdmcp_server_get_status (XDMCPServer *server);

void xdmcp_server_set_key (XDMCPServer *server, const gchar *key);

gboolean xdmcp_server_start (XDMCPServer *server);

G_END_DECLS

#endif /* _XDMCP_SERVER_H_ */
