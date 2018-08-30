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

#ifndef SEAT_XVNC_H_
#define SEAT_XVNC_H_

#include <glib-object.h>
#include "seat.h"

G_BEGIN_DECLS

#define SEAT_XVNC_TYPE (seat_xvnc_get_type())
#define SEAT_XVNC(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_XVNC_TYPE, SeatXVNC))

typedef struct
{
    Seat parent_instance;
} SeatXVNC;

typedef struct
{
    SeatClass parent_class;
} SeatXVNCClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SeatXVNC, g_object_unref)

GType seat_xvnc_get_type (void);

SeatXVNC *seat_xvnc_new (GSocket *connection);

G_END_DECLS

#endif /* SEAT_XVNC_H_ */
