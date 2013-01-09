/*
 * Copyright (C) 2012 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _SEAT_MIR_H_
#define _SEAT_MIR_H_

#include <glib-object.h>
#include "seat.h"

G_BEGIN_DECLS

#define SEAT_MIR_TYPE (seat_mir_get_type())
#define SEAT_MIR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_MIR_TYPE, SeatMir))

typedef struct SeatMirPrivate SeatMirPrivate;

typedef struct
{
    Seat               parent_instance;
    SeatMirPrivate *priv;
} SeatMir;

typedef struct
{
    SeatClass parent_class;
} SeatMirClass;

GType seat_mir_get_type (void);

G_END_DECLS

#endif /* _SEAT_MIR_H_ */
