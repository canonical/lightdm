/*
 * Copyright (C) 2010 Canonical Ltd.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include "display-manager.h"

struct DisplayManagerPrivate
{
    GList *displays;
};

G_DEFINE_TYPE (DisplayManager, display_manager, G_TYPE_OBJECT);

DisplayManager *
display_manager_new (void)
{
    return g_object_new (DISPLAY_MANAGER_TYPE, NULL);
}

static void
display_exited_cb (Display *display, DisplayManager *manager)
{
    manager->priv->displays = g_list_remove (manager->priv->displays, display);
    // FIXME: Check for respawn loops
    if (!manager->priv->displays)
        display_manager_add_display (manager);
}

Display *
display_manager_add_display (DisplayManager *manager)
{
    Display *display;

    display = display_new ();
    g_signal_connect (G_OBJECT (display), "exited", G_CALLBACK (display_exited_cb), manager);

    return display;
}

static void
display_manager_init (DisplayManager *manager)
{
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, DISPLAY_MANAGER_TYPE, DisplayManagerPrivate);
    display_manager_add_display (manager);
}

static void
display_manager_class_init (DisplayManagerClass *klass)
{
    g_type_class_add_private (klass, sizeof (DisplayManagerPrivate));  
}
