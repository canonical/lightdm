/*
 * Copyright (C) 2012-2013 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _SEAT_UNITY_H_
#define _SEAT_UNITY_H_

#include <glib-object.h>
#include "seat.h"

G_BEGIN_DECLS

#define SEAT_UNITY_TYPE (seat_unity_get_type())
#define SEAT_UNITY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_UNITY_TYPE, SeatUnity))

typedef struct SeatUnityPrivate SeatUnityPrivate;

typedef struct
{
    Seat              parent_instance;
    SeatUnityPrivate *priv;
} SeatUnity;

typedef struct
{
    SeatClass parent_class;
} SeatUnityClass;

GType seat_unity_get_type (void);

G_END_DECLS

#endif /* _SEAT_UNITY_H_ */
