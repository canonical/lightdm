/*
 * Copyright (C) 2012-2013 Robert Ancell.
 * Copyright (C) 2020 UBports Foundation.
 * Author(s): Robert Ancell <robert.ancell@canonical.com>
 *            Marius Gripsgard <marius@ubports.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _SEAT_WAYLAND_SYSTEM_COMPOSITOR_H_
#define _SEAT_WAYLAND_SYSTEM_COMPOSITOR_H_

#include <glib-object.h>
#include "seat.h"

G_BEGIN_DECLS

#define SEAT_WAYLAND_SYSTEM_COMPOSITOR_TYPE (seat_wayland_system_compositor_get_type())
#define SEAT_WAYLAND_SYSTEM_COMPOSITOR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_WAYLAND_SYSTEM_COMPOSITOR_TYPE, SeatWaylandSystemCompositor))

typedef struct
{
    Seat parent_instance;
} SeatWaylandSystemCompositor;

typedef struct
{
    SeatClass parent_class;
} SeatWaylandSystemCompositorClass;

GType seat_wayland_system_compositor_get_type (void);

G_END_DECLS

#endif /* _SEAT_WAYLAND_SYSTEM_COMPOSITOR_H_ */
