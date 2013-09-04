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

#ifndef _XSERVER_XVNC_H_
#define _XSERVER_XVNC_H_

#include "xserver.h"

G_BEGIN_DECLS

#define XSERVER_XVNC_TYPE    (xserver_xvnc_get_type())
#define XSERVER_XVNC(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), XSERVER_XVNC_TYPE, XServerXVNC))
#define IS_XSERVER_XVNC(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XSERVER_XVNC_TYPE))

typedef struct XServerXVNCPrivate XServerXVNCPrivate;

typedef struct
{
    XServer              parent_instance;
    XServerXVNCPrivate *priv;
} XServerXVNC;

typedef struct
{
    XServerClass parent_class;

    void (*ready)(XServerXVNC *server);
} XServerXVNCClass;

GType xserver_xvnc_get_type (void);

gboolean xserver_xvnc_check_available (void);

XServerXVNC *xserver_xvnc_new (void);

void xserver_xvnc_set_socket (XServerXVNC *server, int fd);

int xserver_xvnc_get_socket (XServerXVNC *server);

void xserver_xvnc_set_geometry (XServerXVNC *server, gint width, gint height);

void xserver_xvnc_set_depth (XServerXVNC *server, gint depth);

const gchar *xserver_xvnc_get_authority_file_path (XServerXVNC *server);

G_END_DECLS

#endif /* _XSERVER_XVNC_H_ */
