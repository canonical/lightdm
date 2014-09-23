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

#ifndef SEAT_XREMOTE_H_
#define SEAT_XREMOTE_H_

#include <glib-object.h>
#include "seat.h"

G_BEGIN_DECLS

#define SEAT_XREMOTE_TYPE (seat_xremote_get_type())
#define SEAT_XREMOTE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_XREMOTE_TYPE, SeatXRemote))

typedef struct
{
    Seat               parent_instance;
} SeatXRemote;

typedef struct
{
    SeatClass parent_class;
} SeatXRemoteClass;

GType seat_xremote_get_type (void);

G_END_DECLS

#endif /* SEAT_XREMOTE_H_ */
