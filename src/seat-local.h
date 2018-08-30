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

#ifndef SEAT_LOCAL_H_
#define SEAT_LOCAL_H_

#include <glib-object.h>
#include "seat.h"

G_BEGIN_DECLS

#define SEAT_LOCAL_TYPE (seat_local_get_type())
#define SEAT_LOCAL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_LOCAL_TYPE, SeatLocal))

typedef struct
{
    Seat parent_instance;
} SeatLocal;

typedef struct
{
    SeatClass parent_class;
} SeatLocalClass;

GType seat_local_get_type (void);

G_END_DECLS

#endif /* SEAT_LOCAL_H_ */
