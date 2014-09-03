/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*-
 *
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <gio/gio.h>

#include "login1.h"

#define LOGIN1_SERVICE_NAME "org.freedesktop.login1"
#define LOGIN1_OBJECT_NAME "/org/freedesktop/login1"
#define LOGIN1_MANAGER_INTERFACE_NAME "org.freedesktop.login1.Manager"

void
login1_lock_session (const gchar *session_id)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (session_id != NULL);

    g_debug ("Locking login1 session %s", session_id);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    if (session_id)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              LOGIN1_SERVICE_NAME,
                                              LOGIN1_OBJECT_NAME,
                                              LOGIN1_MANAGER_INTERFACE_NAME,
                                              "LockSession",
                                              g_variant_new ("(s)", session_id),
                                              G_VARIANT_TYPE ("()"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error locking login1 session: %s", error->message);
        g_clear_error (&error);
        if (result)
            g_variant_unref (result);
    }
    g_object_unref (bus);
}

void
login1_unlock_session (const gchar *session_id)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (session_id != NULL);

    g_debug ("Unlocking login1 session %s", session_id);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    if (session_id)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              LOGIN1_SERVICE_NAME,
                                              LOGIN1_OBJECT_NAME,
                                              LOGIN1_MANAGER_INTERFACE_NAME,
                                              "UnlockSession",
                                              g_variant_new ("(s)", session_id),
                                              G_VARIANT_TYPE ("()"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error unlocking login1 session: %s", error->message);
        g_clear_error (&error);
        if (result)
            g_variant_unref (result);
    }
    g_object_unref (bus);
}

void
login1_activate_session (const gchar *session_id)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (session_id != NULL);

    g_debug ("Activating login1 session %s", session_id);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    if (session_id)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              LOGIN1_SERVICE_NAME,
                                              LOGIN1_OBJECT_NAME,
                                              LOGIN1_MANAGER_INTERFACE_NAME,
                                              "ActivateSession",
                                              g_variant_new ("(s)", session_id),
                                              G_VARIANT_TYPE ("()"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error activating login1 session: %s", error->message);
        g_clear_error (&error);
        if (result)
            g_variant_unref (result);
    }
    g_object_unref (bus);
}
