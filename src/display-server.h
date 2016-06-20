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

#ifndef DISPLAY_SERVER_H_
#define DISPLAY_SERVER_H_

#include <glib-object.h>

typedef struct DisplayServer DisplayServer;

#include "logger.h"
#include "session.h"

G_BEGIN_DECLS

#define DISPLAY_SERVER_TYPE (display_server_get_type())
#define DISPLAY_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), DISPLAY_SERVER_TYPE, DisplayServer))
#define DISPLAY_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), DISPLAY_SERVER_TYPE, DisplayServerClass))
#define DISPLAY_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), DISPLAY_SERVER_TYPE, DisplayServerClass))

#define DISPLAY_SERVER_SIGNAL_READY   "ready"
#define DISPLAY_SERVER_SIGNAL_STOPPED "stopped"

typedef struct DisplayServerPrivate DisplayServerPrivate;

struct DisplayServer
{
    GObject               parent_instance;
    DisplayServerPrivate *priv;
};

typedef struct
{
    GObjectClass parent_class;

    void (*ready)(DisplayServer *server);
    void (*stopped)(DisplayServer *server);

    DisplayServer *(*get_parent)(DisplayServer *server);  
    const gchar *(*get_session_type)(DisplayServer *server);
    gboolean (*get_can_share)(DisplayServer *server);
    gint (*get_vt)(DisplayServer *server);
    gboolean (*start)(DisplayServer *server);
    void (*connect_session)(DisplayServer *server, Session *session);
    void (*disconnect_session)(DisplayServer *server, Session *session);
    void (*stop)(DisplayServer *server);
} DisplayServerClass;

GType display_server_get_type (void);

const gchar *display_server_get_session_type (DisplayServer *server);

DisplayServer *display_server_get_parent (DisplayServer *server);

gboolean display_server_get_can_share (DisplayServer *server);

gint display_server_get_vt (DisplayServer *server);

gboolean display_server_start (DisplayServer *server);

gboolean display_server_get_is_ready (DisplayServer *server);

void display_server_connect_session (DisplayServer *server, Session *session);

void display_server_disconnect_session (DisplayServer *server, Session *session);

void display_server_stop (DisplayServer *server);

gboolean display_server_get_is_stopping (DisplayServer *server);

G_END_DECLS

#endif /* DISPLAY_SERVER_H_ */
