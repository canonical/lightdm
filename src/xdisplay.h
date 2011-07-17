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

#ifndef _XDISPLAY_H_
#define _XDISPLAY_H_

#include "display.h"
#include "xserver.h"

G_BEGIN_DECLS

#define XDISPLAY_TYPE (xdisplay_get_type())
#define XDISPLAY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XDISPLAY_TYPE, XDisplay));

typedef struct
{
    Display          parent_instance;
} XDisplay;

typedef struct
{
    DisplayClass parent_class;
} XDisplayClass;

GType xdisplay_get_type (void);

XDisplay *xdisplay_new (const gchar *config_section, XServer *server);

G_END_DECLS

#endif /* _XDISPLAY_H_ */
