/*
 * Copyright (C) 2013 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _SEAT_SURFACEFLINGER_H_
#define _SEAT_SURFACEFLINGER_H_

#include <glib-object.h>
#include "seat.h"

G_BEGIN_DECLS

#define SEAT_SURFACEFLINGER_TYPE (seat_surfaceflinger_get_type())
#define SEAT_SURFACEFLINGER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_SURFACEFLINGER_TYPE, SeatSurfaceflinger))

typedef struct SeatSurfaceflingerPrivate SeatSurfaceflingerPrivate;

typedef struct
{
    Seat              parent_instance;
    SeatSurfaceflingerPrivate *priv;
} SeatSurfaceflinger;

typedef struct
{
    SeatClass parent_class;
} SeatSurfaceflingerClass;

GType seat_surfaceflinger_get_type (void);

G_END_DECLS

#endif /* _SEAT_SURFACEFLINGER_H_ */
