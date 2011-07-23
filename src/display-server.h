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

#ifndef _DISPLAY_SERVER_H_
#define _DISPLAY_SERVER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define DISPLAY_SERVER_TYPE (display_server_get_type())
#define DISPLAY_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), DISPLAY_SERVER_TYPE, DisplayServer))
#define DISPLAY_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), DISPLAY_SERVER_TYPE, DisplayServerClass))
#define DISPLAY_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), DISPLAY_SERVER_TYPE, DisplayServerClass))

typedef struct DisplayServerPrivate DisplayServerPrivate;

typedef struct
{
    GObject               parent_instance;
    DisplayServerPrivate *priv;
} DisplayServer;

typedef struct
{
    GObjectClass parent_class;

    void (*ready)(DisplayServer *server);
    void (*stopped)(DisplayServer *server);

    gboolean (*start)(DisplayServer *server);
    void (*stop)(DisplayServer *server);
} DisplayServerClass;

GType display_server_get_type (void);

gboolean display_server_start (DisplayServer *server);

void display_server_stop (DisplayServer *server);

G_END_DECLS

#endif /* _DISPLAY_SERVER_H_ */
