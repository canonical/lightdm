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

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>

#include "display-manager.h"

struct DisplayManagerPrivate
{
    DBusGConnection *connection;

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
    dbus_g_connection_register_g_object (manager->priv->connection, "/org/gnome/LightDisplayManager", G_OBJECT (display));
    //g_signal_connect (G_OBJECT (display), "exited", G_CALLBACK (display_exited_cb), manager);

    return display;
}

static void
display_manager_init (DisplayManager *manager)
{
    GError *error = NULL;
    DBusGProxy *proxy;
    guint result;

    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, DISPLAY_MANAGER_TYPE, DisplayManagerPrivate);
  
    manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
    if (!manager->priv->connection)
        g_warning ("Failed to register on D-Bus: %s", error->message);
    g_clear_error (&error);

    proxy = dbus_g_proxy_new_for_name (manager->priv->connection,
                                       DBUS_SERVICE_DBUS,
                                       DBUS_PATH_DBUS,
                                       DBUS_INTERFACE_DBUS);
    if (!org_freedesktop_DBus_request_name (proxy,
                                            "org.gnome.LightDisplayManager",
                                            DBUS_NAME_FLAG_DO_NOT_QUEUE, &result, &error))
        g_warning ("Failed to register D-Bus name: %s", error->message);
    g_object_unref (proxy);

    display_manager_add_display (manager);
}

static void
display_manager_class_init (DisplayManagerClass *klass)
{
    g_type_class_add_private (klass, sizeof (DisplayManagerPrivate));  
}
