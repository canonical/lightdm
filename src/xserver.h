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

#include "child-process.h"
#include "xauth.h"

G_BEGIN_DECLS

#define XSERVER_TYPE (xserver_get_type())
#define XSERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XSERVER_TYPE, XServer))

typedef struct XServerPrivate XServerPrivate;

typedef struct
{
    ChildProcess    parent_instance;
    XServerPrivate *priv;
} XServer;

typedef struct
{
    ChildProcessClass parent_class;

    void (*ready)(XServer *server);  
} XServerClass;

typedef enum
{
    /* Local server */
    XSERVER_TYPE_LOCAL,
  
    /* Local server active as a terminal to a remote display manager */
    XSERVER_TYPE_LOCAL_TERMINAL,

    /* Remote server */
    XSERVER_TYPE_REMOTE
} XServerType;

guint xserver_get_free_display_number (void);

void xserver_release_display_number (guint number);

GType xserver_get_type (void);

XServer *xserver_new (const gchar *config_section, XServerType type, const gchar *hostname, gint display_number);

XServerType xserver_get_server_type (XServer *server);

void xserver_set_port (XServer *server, guint port);

gint xserver_get_display_number (XServer *server);

const gchar *xserver_get_address (XServer *server);

void xserver_set_authentication (XServer *server, const gchar *name, const guchar *data, gsize data_length);

void xserver_set_authorization (XServer *server, XAuthorization *authorization);

XAuthorization *xserver_get_authorization (XServer *server);

gint xserver_get_vt (XServer *server);

gboolean xserver_start (XServer *server);

gboolean xserver_get_is_running (XServer *server);

void xserver_disconnect_clients (XServer *server);

void xserver_stop (XServer *server);

G_END_DECLS

#endif /* _XSERVER_H_ */
