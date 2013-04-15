/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*-
 *
 * Copyright (C) 2010-2011 Robert Ancell,
 *               2013      Iain Lane
 * Authors: Robert Ancell <robert.ancell@canonical.com>,
 *          Iain Lane     <iain.lane@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <sys/types.h>
#include <unistd.h>
#include <gio/gio.h>

#include "logind.h"

gchar *
logind_get_session_id ()
{
    GDBusConnection *bus;
    GDBusProxy *proxy;
    gchar *session_path;
    gchar *result_s = NULL;
    GError *error = NULL;

    /* Always successful */
    pid_t pid = getpid();

    g_debug ("Retrieving logind session for pid %u", pid);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return NULL;

    GVariant *result;

    result = g_dbus_connection_call_sync (bus,
                                          "org.freedesktop.login1",
                                          "/org/freedesktop/login1",
                                          "org.freedesktop.login1.Manager",
                                          "GetSessionByPID",
                                          g_variant_new ("(u)", pid),
                                          G_VARIANT_TYPE ("(o)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);

    g_object_unref (bus);
    if (error)
      g_warning ("Error getting logind session id: %s", error->message);
    g_clear_error (&error);
    if (!result)
      return NULL;
    
    g_variant_get (result, "(o)", &session_path);
    g_variant_unref (result);

    return session_path;
      
}

void
logind_lock_session (const gchar *id)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (id != NULL);

    g_debug ("Locking logind session %s", id);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    GVariant *result;

    result = g_dbus_connection_call_sync (bus,
                                          "org.freedesktop.login1",
                                          "/org/freedesktop/login1",
                                          "org.freedesktop.login1.Manager",
                                          "LockSession",
                                          g_variant_new ("(s)", 
                                              g_variant_new_string(id)),
                                          G_VARIANT_TYPE ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    if (error)
      g_warning ("Error locking logind session: %s", error->message);
    g_clear_error (&error);
    if (result)
      g_variant_unref (result);

    g_object_unref (bus);
}

void
logind_unlock_session (const gchar *id)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (id != NULL);

    g_debug ("Unlocking logind session %s", id);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    GVariant *result;

    result = g_dbus_connection_call_sync (bus,
                                          "org.freedesktop.login1",
                                          "/org/freedesktop/login1",
                                          "org.freedesktop.login1.Manager",
                                          "UnlockSession",
                                          g_variant_new ("(s)", 
                                              g_variant_new_string(id)),
                                          G_VARIANT_TYPE ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    if (error)
      g_warning ("Error unlocking logind session: %s", error->message);
    g_clear_error (&error);
    if (result)
      g_variant_unref (result);

    g_object_unref (bus);
}
