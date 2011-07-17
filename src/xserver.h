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

#ifndef _XSERVER_H_
#define _XSERVER_H_

#include <glib-object.h>
#include "session.h"
#include "xauth.h"

G_BEGIN_DECLS

#define XSERVER_TYPE (xserver_get_type())
#define XSERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XSERVER_TYPE, XServer))
#define XSERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), XSERVER_TYPE, XServerClass))
#define XSERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), XSERVER_TYPE, XServerClass))

typedef struct XServerPrivate XServerPrivate;

typedef struct
{
    GObject         parent_instance;
    XServerPrivate *priv;
} XServer;

typedef struct
{
    GObjectClass parent_class;

    void (*ready)(XServer *server);
    void (*stopped)(XServer *server);

    void (*setup_session)(XServer *server, Session *session);
    gboolean (*start)(XServer *server);
    gboolean (*restart)(XServer *server);
    void (*stop)(XServer *server);
} XServerClass;

GType xserver_get_type (void);

void xserver_set_hostname (XServer *server, const gchar *hostname);

gchar *xserver_get_hostname (XServer *server);

void xserver_set_display_number (XServer *server, guint number);

guint xserver_get_display_number (XServer *server);

const gchar *xserver_get_address (XServer *server);

void xserver_set_authentication (XServer *server, const gchar *name, const guint8 *data, gsize data_length);

const gchar *xserver_get_authentication_name (XServer *server);

const guint8 *xserver_get_authentication_data (XServer *server);

gsize xserver_get_authentication_data_length (XServer *server);

void xserver_set_authorization (XServer *server, XAuthorization *authorization);

XAuthorization *xserver_get_authorization (XServer *server);

GFile *xserver_get_authority_file (XServer *server);

void xserver_setup_session (XServer *server, Session *session);

gboolean xserver_start (XServer *server);

gboolean xserver_connect (XServer *server);

gboolean xserver_restart (XServer *server);

void xserver_disconnect (XServer *server);

void xserver_stop (XServer *server);

G_END_DECLS

#endif /* _XSERVER_H_ */
