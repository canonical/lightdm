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

#ifndef DISPLAY_MANAGER_H_
#define DISPLAY_MANAGER_H_

#include <glib-object.h>

#include "seat.h"

G_BEGIN_DECLS

#define DISPLAY_MANAGER_TYPE (display_manager_get_type())
#define DISPLAY_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), DISPLAY_MANAGER_TYPE, DisplayManager));

#define DISPLAY_MANAGER_SIGNAL_SEAT_ADDED   "seat-added"
#define DISPLAY_MANAGER_SIGNAL_SEAT_REMOVED "seat-removed"
#define DISPLAY_MANAGER_SIGNAL_STOPPED      "stopped"

typedef struct
{
    GObject parent_instance; // 管理对象的生命周期
} DisplayManager;

typedef struct
{
    GObjectClass parent_class;

    void (*seat_added)(DisplayManager *manager, Seat *seat);
    void (*seat_removed)(DisplayManager *manager, Seat *seat);
    void (*stopped)(DisplayManager *manager);
} DisplayManagerClass;

GType display_manager_get_type (void);

DisplayManager *display_manager_new (void);

gboolean display_manager_add_seat (DisplayManager *manager, Seat *seat);

GList *display_manager_get_seats (DisplayManager *manager);

Seat *display_manager_get_seat (DisplayManager *manager, const gchar *name);

void display_manager_start (DisplayManager *manager);

void display_manager_stop (DisplayManager *manager);

G_END_DECLS

#endif /* DISPLAY_MANAGER_H_ */
