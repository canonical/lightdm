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

#ifndef _DISPLAY_MANAGER_H_
#define _DISPLAY_MANAGER_H_

#include <glib-object.h>
#include "display.h"

G_BEGIN_DECLS

#define DISPLAY_MANAGER_TYPE (display_manager_get_type())
#define DISPLAY_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), DISPLAY_MANAGER_TYPE, DisplayManager));

typedef struct DisplayManagerPrivate DisplayManagerPrivate;

typedef struct
{
    GObject         parent_instance;
    DisplayManagerPrivate *priv;
} DisplayManager;

typedef struct
{
    GObjectClass parent_class;

    void (*display_added)(DisplayManager *manager, Display *display);
} DisplayManagerClass;

GType display_manager_get_type (void);

DisplayManager *display_manager_new (GKeyFile *config);

Display *display_manager_add_display (DisplayManager *manager);

gboolean display_manager_switch_to_user (DisplayManager *manager, char *username, GError *error);

void display_manager_start (DisplayManager *manager);

G_END_DECLS

#endif /* _DISPLAY_MANAGER_H_ */
