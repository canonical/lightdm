/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _THEME_H_
#define _THEME_H_

#include <glib.h>

GKeyFile *load_theme (const gchar *name, GError **error);

gchar *theme_get_command (GKeyFile *theme);

#endif /* _THEME_H_ */
