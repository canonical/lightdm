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

static GDBusProxy *ck_proxy = NULL;

static gboolean
load_ck_proxy (void)
{
    if (!ck_proxy)
    {
        GError *error = NULL;
        ck_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  NULL,
                                                  "org.freedesktop.ConsoleKit",
                                                  "/org/freedesktop/ConsoleKit/Manager",
                                                  "org.freedesktop.ConsoleKit.Manager",
                                                  NULL, &error);
        if (!ck_proxy)
            g_warning ("Unable to get connection to ConsoleKit: %s", error->message);
        g_clear_error (&error);
    }

    return ck_proxy != NULL;
}

gchar *
ck_open_session (GVariantBuilder *parameters)
{
    GVariant *result;
    gchar *cookie = NULL;
    GError *error = NULL;

    g_return_val_if_fail (parameters != NULL, NULL);

    if (!load_ck_proxy ())
        return FALSE;

    result = g_dbus_proxy_call_sync (ck_proxy,
                                     "OpenSessionWithParameters",
                                     g_variant_new ("(a(sv))", parameters),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);

    if (!result)
        g_warning ("Failed to open CK session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return NULL;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(s)")))
        g_variant_get (result, "(s)", &cookie);
    else
        g_warning ("Unexpected response from OpenSessionWithParameters: %s", g_variant_get_type_string (result));
    g_variant_unref (result);

    if (cookie)
        g_debug ("Opened ConsoleKit session %s", cookie);

    return cookie;
}

void
ck_unlock_session (const gchar *cookie)
{
    GVariant *result;
    GDBusProxy *proxy;
    gchar *session_path = NULL;
    GError *error = NULL;

    g_return_if_fail (cookie != NULL);

    if (!load_ck_proxy ())
        return;

    g_debug ("Unlocking ConsoleKit session %s", cookie);

    result = g_dbus_proxy_call_sync (ck_proxy,
                                     "GetSessionForCookie",
                                     g_variant_new ("(s)", cookie),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    if (!result)
        g_warning ("Error getting ConsoleKit session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(o)")))
        g_variant_get (result, "(o)", &session_path);
    else
        g_warning ("Unexpected response from GetSessionForCookie: %s", g_variant_get_type_string (result));
    g_variant_unref (result);
    if (!session_path)
        return;

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.ConsoleKit",
                                           session_path,
                                           "org.freedesktop.ConsoleKit.Session",
                                           NULL, &error);
    g_free (session_path);
  
    if (!proxy)
        g_warning ("Unable to get connection to ConsoleKit session: %s", error->message);
    g_clear_error (&error);
    if (!proxy)
        return;

    result = g_dbus_proxy_call_sync (proxy,
                                     "Unlock",
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    g_object_unref (proxy);

    if (!result)
        g_warning ("Error unlocking ConsoleKit session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    g_variant_unref (result);
    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.ConsoleKit",
                                           session_path,
                                           "org.freedesktop.ConsoleKit.Session",
                                           NULL, &error);
    if (!proxy)
        g_warning ("Unable to get connection to ConsoleKit session: %s", error->message);
    g_variant_unref (result);
    g_clear_error (&error);
    if (!proxy)
        return;

    result = g_dbus_proxy_call_sync (proxy,
                                     "Unlock",
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    g_object_unref (proxy);

    if (!result)
        g_warning ("Error unlocking ConsoleKit session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    g_variant_unref (result);
}

void
ck_close_session (const gchar *cookie)
{
    GVariant *result;
    GError *error = NULL;

    g_return_if_fail (cookie != NULL);

    if (!load_ck_proxy ())
        return;

    g_debug ("Ending ConsoleKit session %s", cookie);

    result = g_dbus_proxy_call_sync (ck_proxy,
                                     "CloseSession",
                                     g_variant_new ("(s)", cookie),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);

    if (!result)
        g_warning ("Error ending ConsoleKit session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(b)")))
    {
        gboolean is_closed;
        g_variant_get (result, "(b)", &is_closed);
        if (!is_closed)
            g_warning ("ConsoleKit.Manager.CloseSession() returned false");
    }
    else
        g_warning ("Unexpected response from CloseSession: %s", g_variant_get_type_string (result));

    g_variant_unref (result);
}
