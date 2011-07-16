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

#ifndef _PLYMOUTH_H_
#define _PLYMOUTH_H_

#include <glib-object.h>

G_BEGIN_DECLS

gboolean plymouth_get_is_running (void);

gboolean plymouth_get_is_active (void);

gboolean plymouth_has_active_vt (void);

void plymouth_deactivate (void);

void plymouth_quit (gboolean retain_splash);

G_END_DECLS

#endif /* _PLYMOUTH_H_ */
