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

#ifndef SEAT_XLOCAL_H_
#define SEAT_XLOCAL_H_

#include <glib-object.h>
#include "seat.h"

G_BEGIN_DECLS

#define SEAT_XLOCAL_TYPE (seat_xlocal_get_type())
#define SEAT_XLOCAL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_XLOCAL_TYPE, SeatXLocal))

typedef struct SeatXLocalPrivate SeatXLocalPrivate;

typedef struct
{
    Seat               parent_instance;
    SeatXLocalPrivate *priv;
} SeatXLocal;

typedef struct
{
    SeatClass parent_class;
} SeatXLocalClass;

GType seat_xlocal_get_type (void);

G_END_DECLS

#endif /* SEAT_XLOCAL_H_ */
