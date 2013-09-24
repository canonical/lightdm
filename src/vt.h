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

#ifndef VT_H_
#define VT_H_

#include <glib-object.h>

gboolean vt_can_multi_seat (void);

gint vt_get_active (void);

gint vt_get_unused (void);

gint vt_get_min (void);

void vt_ref (gint number);

void vt_unref (gint number);

void vt_set_active (gint number);

#endif /* VT_H_ */
