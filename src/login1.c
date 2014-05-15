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

static gboolean
check_login1 (void)
{
    GDBusProxy *proxy;
    gchar *owner;
    gboolean success;

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                           NULL,
                                           "org.freedesktop.login1",
                                           "/org/freedesktop/login1",
                                           "org.freedesktop.login1.Manager",
                                           NULL,
                                           NULL);
    if (!proxy)
        return FALSE;

    owner = g_dbus_proxy_get_name_owner (proxy);
    g_object_unref (proxy);

    success = (owner != NULL);
    g_free (owner);

    return success;
}

gboolean
login1_is_running (void)
{
    static gboolean have_checked = FALSE;
    static gboolean is_running = FALSE;

    if (!have_checked)
    {
        have_checked = TRUE;
        is_running = check_login1();
    }

    return is_running;
}

gchar *
login1_get_session_id (void)
{
    GDBusConnection *bus;
    GVariant *result;
    gchar *session_path;
    GError *error = NULL;

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return NULL;
    result = g_dbus_connection_call_sync (bus,
                                          "org.freedesktop.login1",
                                          "/org/freedesktop/login1",
                                          "org.freedesktop.login1.Manager",
                                          "GetSessionByPID",
                                          g_variant_new ("(u)", getpid()),
                                          G_VARIANT_TYPE ("(o)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    g_object_unref (bus);

    if (error)
        g_warning ("Failed to open login1 session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return NULL;

    g_variant_get (result, "(o)", &session_path);
    g_variant_unref (result);
    g_debug ("Got login1 session id: %s", session_path);

    return session_path;
}

void
login1_lock_session (const gchar *session_path)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (session_path != NULL);

    g_debug ("Locking login1 session %s", session_path);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              "org.freedesktop.login1",
                                              session_path,
                                              "org.freedesktop.login1.Session",
                                              "Lock",
                                              g_variant_new ("()"),
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
login1_unlock_session (const gchar *session_path)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (session_path != NULL);

    g_debug ("Unlocking login1 session %s", session_path);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              "org.freedesktop.login1",
                                              session_path,
                                              "org.freedesktop.login1.Session",
                                              "Unlock",
                                              g_variant_new ("()"),
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
login1_activate_session (const gchar *session_path)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (session_path != NULL);

    g_debug ("Activating login1 session %s", session_path);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              "org.freedesktop.login1",
                                              session_path,
                                              "org.freedesktop.login1.Session",
                                              "Activate",
                                              g_variant_new ("()"),
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
