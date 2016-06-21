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

#ifndef X_SERVER_XVNC_H_
#define X_SERVER_XVNC_H_

#include "x-server-local.h"

G_BEGIN_DECLS

#define X_SERVER_XVNC_TYPE    (x_server_xvnc_get_type())
#define X_SERVER_XVNC(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), X_SERVER_XVNC_TYPE, XServerXVNC))
#define IS_X_SERVER_XVNC(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), X_SERVER_XVNC_TYPE))

typedef struct XServerXVNCPrivate XServerXVNCPrivate;

typedef struct
{
    XServerLocal        parent_instance;
    XServerXVNCPrivate *priv;
} XServerXVNC;

typedef struct
{
    XServerLocalClass parent_class;

    void (*ready)(XServerXVNC *server);
} XServerXVNCClass;

GType x_server_xvnc_get_type (void);

gboolean x_server_xvnc_check_available (void);

XServerXVNC *x_server_xvnc_new (void);

void x_server_xvnc_set_socket (XServerXVNC *server, int fd);

int x_server_xvnc_get_socket (XServerXVNC *server);

void x_server_xvnc_set_geometry (XServerXVNC *server, gint width, gint height);

void x_server_xvnc_set_depth (XServerXVNC *server, gint depth);

G_END_DECLS

#endif /* X_SERVER_XVNC_H_ */
