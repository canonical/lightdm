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

#ifndef _XSERVER_LOCAL_H_
#define _XSERVER_LOCAL_H_

#include "xserver.h"

G_BEGIN_DECLS

#define XSERVER_LOCAL_TYPE    (xserver_local_get_type())
#define XSERVER_LOCAL(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), XSERVER_LOCAL_TYPE, XServerLocal))
#define IS_XSERVER_LOCAL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XSERVER_LOCAL_TYPE))

typedef struct XServerLocalPrivate XServerLocalPrivate;

typedef struct
{
    XServer              parent_instance;
    XServerLocalPrivate *priv;
} XServerLocal;

typedef struct
{
    XServerClass parent_class;

    void (*ready)(XServerLocal *server);
} XServerLocalClass;

guint xserver_local_get_unused_display_number (void);

void xserver_local_release_display_number (guint display_number);

GType xserver_local_get_type (void);

XServerLocal *xserver_local_new (void);

void xserver_local_set_command (XServerLocal *server, const gchar *command);

void xserver_local_set_config (XServerLocal *server, const gchar *path);

void xserver_local_set_layout (XServerLocal *server, const gchar *layout);

void xserver_local_set_allow_tcp (XServerLocal *server, gboolean allow_tcp);

void xserver_local_set_xdmcp_server (XServerLocal *server, const gchar *hostname);

const gchar *xserver_local_get_xdmcp_server (XServerLocal *server);

void xserver_local_set_xdmcp_port (XServerLocal *server, guint port);

guint xserver_local_get_xdmcp_port (XServerLocal *server);

void xserver_local_set_xdmcp_key (XServerLocal *server, const gchar *key);

gint xserver_local_get_vt (XServerLocal *server);

gchar *xserver_local_get_authority_file_path (XServerLocal *server);

G_END_DECLS

#endif /* _XSERVER_LOCAL_H_ */
