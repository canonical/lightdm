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

#ifndef _DMRC_H_
#define _DMRC_H_

#include <glib.h>

G_BEGIN_DECLS

GKeyFile *dmrc_load (const gchar *username);

void dmrc_save (GKeyFile *dmrc_file, const gchar *username);

G_END_DECLS

#endif /* _DMRC_H_ */
