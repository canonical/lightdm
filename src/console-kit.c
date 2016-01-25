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

#include "console-kit.h"

gchar *
ck_open_session (GVariantBuilder *parameters)
{
    GDBusConnection *bus;
    GVariant *result;
    gchar *cookie;
    GError *error = NULL;

    g_return_val_if_fail (parameters != NULL, NULL);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return NULL;
    result = g_dbus_connection_call_sync (bus,
                                          "org.freedesktop.ConsoleKit",
                                          "/org/freedesktop/ConsoleKit/Manager",
                                          "org.freedesktop.ConsoleKit.Manager",
                                          "OpenSessionWithParameters",
                                          g_variant_new ("(a(sv))", parameters),
                                          G_VARIANT_TYPE ("(s)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    g_object_unref (bus);

    if (error)
        g_warning ("Failed to open CK session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return NULL;

    g_variant_get (result, "(s)", &cookie);
    g_variant_unref (result);
    g_debug ("Opened ConsoleKit session %s", cookie);

    return cookie;
}

static gchar *
get_ck_session (GDBusConnection *bus, const gchar *cookie)
{
    GVariant *result;
    gchar *session_path;
    GError *error = NULL;

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    result = g_dbus_connection_call_sync (bus,
                                          "org.freedesktop.ConsoleKit",
                                          "/org/freedesktop/ConsoleKit/Manager",
                                          "org.freedesktop.ConsoleKit.Manager",
                                          "GetSessionForCookie",
                                          g_variant_new ("(s)", cookie),
                                          G_VARIANT_TYPE ("(o)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    g_object_unref (bus);
    if (error)
        g_warning ("Error getting ConsoleKit session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return NULL;

    g_variant_get (result, "(o)", &session_path);
    g_variant_unref (result);

    return session_path;
}

void
ck_lock_session (const gchar *cookie)
{
    GDBusConnection *bus;
    gchar *session_path;
    GError *error = NULL;

    g_return_if_fail (cookie != NULL);

    g_debug ("Locking ConsoleKit session %s", cookie);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    session_path = get_ck_session (bus, cookie);
    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              "org.freedesktop.ConsoleKit",
                                              session_path,
                                              "org.freedesktop.ConsoleKit.Session",
                                              "Lock",
                                              g_variant_new ("()"),
                                              G_VARIANT_TYPE ("()"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error locking ConsoleKit session: %s", error->message);
        g_clear_error (&error);
        if (result)
            g_variant_unref (result);
    }
    g_object_unref (bus);
}

void
ck_unlock_session (const gchar *cookie)
{
    GDBusConnection *bus;
    gchar *session_path;
    GError *error = NULL;

    g_return_if_fail (cookie != NULL);

    g_debug ("Unlocking ConsoleKit session %s", cookie);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    session_path = get_ck_session (bus, cookie);
    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              "org.freedesktop.ConsoleKit",
                                              session_path,
                                              "org.freedesktop.ConsoleKit.Session",
                                              "Unlock",
                                              g_variant_new ("()"),
                                              G_VARIANT_TYPE ("()"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error unlocking ConsoleKit session: %s", error->message);
        g_clear_error (&error);
        if (result)
            g_variant_unref (result);
    }
    g_object_unref (bus);
}

void
ck_activate_session (const gchar *cookie)
{
    GDBusConnection *bus;
    gchar *session_path;
    GError *error = NULL;

    g_return_if_fail (cookie != NULL);

    g_debug ("Activating ConsoleKit session %s", cookie);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    session_path = get_ck_session (bus, cookie);
    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              "org.freedesktop.ConsoleKit",
                                              session_path,
                                              "org.freedesktop.ConsoleKit.Session",
                                              "Activate",
                                              g_variant_new ("()"),
                                              G_VARIANT_TYPE ("()"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error activating ConsoleKit session: %s", error->message);
        g_clear_error (&error);
        if (result)
            g_variant_unref (result);
    }
    g_object_unref (bus);
}

void
ck_close_session (const gchar *cookie)
{
    GDBusConnection *bus;
    GVariant *result;
    gboolean is_closed;
    GError *error = NULL;

    g_return_if_fail (cookie != NULL);

    g_debug ("Ending ConsoleKit session %s", cookie);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;
    result = g_dbus_connection_call_sync (bus,
                                          "org.freedesktop.ConsoleKit",
                                          "/org/freedesktop/ConsoleKit/Manager",
                                          "org.freedesktop.ConsoleKit.Manager",
                                          "CloseSession",
                                          g_variant_new ("(s)", cookie),
                                          G_VARIANT_TYPE ("(b)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    g_object_unref (bus);

    if (error)
        g_warning ("Error ending ConsoleKit session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    g_variant_get (result, "(b)", &is_closed);
    g_variant_unref (result);

    if (!is_closed)
        g_warning ("ConsoleKit.Manager.CloseSession() returned false");
}

gchar *
ck_get_xdg_runtime_dir (const gchar *cookie)
{
    GDBusConnection *bus;
    gchar *session_path;
    gchar *runtime_dir = NULL;
    GError *error = NULL;

    g_return_if_fail (cookie != NULL);

    g_debug ("Getting XDG_RUNTIME_DIR from ConsoleKit for session %s", cookie);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return NULL;

    session_path = get_ck_session (bus, cookie);
    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              "org.freedesktop.ConsoleKit",
                                              session_path,
                                              "org.freedesktop.ConsoleKit.Session",
                                              "GetXDGRuntimeDir",
                                              g_variant_new ("()"),
                                              G_VARIANT_TYPE ("(s)"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error getting XDG_RUNTIME_DIR from ConsoleKit: %s", error->message);
        g_clear_error (&error);
        if (!result)
            return NULL;

        g_variant_get (result, "(s)", &runtime_dir);
        g_variant_unref (result);
        g_debug ("ConsoleKit XDG_RUNTIME_DIR is %s", runtime_dir);
    }

    g_object_unref (bus);
    return runtime_dir;
}
