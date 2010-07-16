/*
 * Copyright (C) 2010 Robert Ancell.
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
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#define XSERVER_TYPE (xserver_get_type())
#define XSERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XSERVER_TYPE, XServer));

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
    void (*exited)(XServer *server);
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

GType xserver_get_type (void);

void xserver_handle_signal (GPid pid);

XServer *xserver_new (XServerType type, const gchar *hostname, gint display_number);

XServerType xserver_get_server_type (XServer *server);

void xserver_set_command (XServer *server, const gchar *command);

const gchar *xserver_get_command (XServer *server);

void xserver_set_port (XServer *server, guint port);

guint xserver_get_port (XServer *server);

void xserver_set_cookie (XServer *server, const gchar *cookie);

const gchar *xserver_get_cookie (XServer *server);

const gchar *xserver_get_hostname (XServer *server);

gint xserver_get_display_number (XServer *server);

const gchar *xserver_get_address (XServer *server);

gboolean xserver_start (XServer *server);

G_END_DECLS

#endif /* _XSERVER_H_ */
