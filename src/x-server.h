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

#ifndef X_SERVER_H_
#define X_SERVER_H_

#include <glib-object.h>
#include "display-server.h"
#include "x-authority.h"

G_BEGIN_DECLS

#define X_SERVER_TYPE (x_server_get_type())
#define X_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), X_SERVER_TYPE, XServer))
#define X_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), X_SERVER_TYPE, XServerClass))
#define X_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), X_SERVER_TYPE, XServerClass))
#define IS_X_SERVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), X_SERVER_TYPE))

typedef struct XServerPrivate XServerPrivate;

typedef struct
{
    DisplayServer   parent_instance;
    XServerPrivate *priv;
} XServer;

typedef struct
{
    DisplayServerClass parent_class;
} XServerClass;

GType x_server_get_type (void);

void x_server_set_hostname (XServer *server, const gchar *hostname);

gchar *x_server_get_hostname (XServer *server);

void x_server_set_display_number (XServer *server, guint number);

guint x_server_get_display_number (XServer *server);

const gchar *x_server_get_address (XServer *server);

const gchar *x_server_get_authentication_name (XServer *server);

const guint8 *x_server_get_authentication_data (XServer *server);

gsize x_server_get_authentication_data_length (XServer *server);

void x_server_set_authority (XServer *server, XAuthority *authority);

XAuthority *x_server_get_authority (XServer *server);

G_END_DECLS

#endif /* X_SERVER_H_ */
