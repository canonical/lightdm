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

#ifndef _SEAT_H_
#define _SEAT_H_

#include <glib-object.h>
#include "display.h"

G_BEGIN_DECLS

#define SEAT_TYPE           (seat_get_type())
#define SEAT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_TYPE, Seat))
#define SEAT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), SEAT_TYPE, SeatClass))
#define SEAT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SEAT_TYPE, SeatClass))

typedef struct SeatPrivate SeatPrivate;

typedef struct
{
    GObject         parent_instance;
    SeatPrivate *priv;
} Seat;

typedef struct
{
    GObjectClass parent_class;

    gboolean (*start)(Seat *seat);
    gboolean (*get_can_switch)(Seat *seat); // FIXME: Make a construct property
    Display *(*add_display)(Seat *seat);
    void (*set_active_display)(Seat *seat, Display *display);
    void (*stop)(Seat *seat);

    void (*started)(Seat *seat);
    void (*stopped)(Seat *seat);
} SeatClass;

GType seat_get_type (void);

gboolean seat_start (Seat *seat);

GList *seat_get_displays (Seat *seat);

void seat_remove_display (Seat *seat, Display *display);

gboolean seat_get_can_switch (Seat *seat);

gboolean seat_switch_to_greeter (Seat *seat);

gboolean seat_switch_to_user (Seat *seat, const gchar *username);

gboolean seat_switch_to_guest (Seat *seat);

void seat_stop (Seat *seat);

G_END_DECLS

#endif /* _SEAT_H_ */
