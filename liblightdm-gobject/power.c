/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#include <config.h>

#include <string.h>
#include <gio/gio.h>

#include "lightdm/power.h"

static GDBusProxy *upower_proxy = NULL;
static GDBusProxy *ck_proxy = NULL;

static gboolean
upower_call_function (const gchar *function, gboolean has_result)
{
    GVariant *result;
    gboolean function_result = FALSE;
    GError *error = NULL;

    if (!upower_proxy)
    {
        upower_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                      G_DBUS_PROXY_FLAGS_NONE,
                                                      NULL,
                                                      "org.freedesktop.UPower",
                                                      "/org/freedesktop/UPower",
                                                      "org.freedesktop.UPower",
                                                      NULL,
                                                      &error);
        if (!upower_proxy)
            g_warning ("Error getting UPower proxy: %s", error->message);
        g_clear_error (&error);
        if (!upower_proxy)
            return FALSE;
    }

    result = g_dbus_proxy_call_sync (upower_proxy,
                                     function,
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    if (!result)
        g_warning ("Error calling UPower function %s: %s", function, error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(b)")))
        g_variant_get (result, "(b)", &function_result);

    g_variant_unref (result);
    return function_result;
}

/**
 * lightdm_get_can_suspend:
 *
 * Checks if authorized to do a system suspend.
 *
 * Return value: #TRUE if can suspend the system
 **/
gboolean
lightdm_get_can_suspend (void)
{
    return upower_call_function ("SuspendAllowed", TRUE);
}

/**
 * lightdm_suspend:
 *
 * Triggers a system suspend.
 **/
void
lightdm_suspend (void)
{
    upower_call_function ("Suspend", FALSE);
}

/**
 * lightdm_get_can_hibernate:
 *
 * Checks if is authorized to do a system hibernate.
 *
 * Return value: #TRUE if can hibernate the system
 **/
gboolean
lightdm_get_can_hibernate (void)
{
    return upower_call_function ("HibernateAllowed", TRUE);
}

/**
 * lightdm_hibernate:
 *
 * Triggers a system hibernate.
 **/
void
lightdm_hibernate (void)
{
    upower_call_function ("Hibernate", FALSE);
}

static gboolean
ck_call_function (const gchar *function, gboolean has_result)
{
    GVariant *result;
    gboolean function_result = FALSE;
    GError *error = NULL;

    if (!ck_proxy)
    {
        ck_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  NULL,
                                                  "org.freedesktop.ConsoleKit",
                                                  "/org/freedesktop/ConsoleKit/Manager",
                                                  "org.freedesktop.ConsoleKit.Manager",
                                                  NULL,
                                                  &error);
        if (!ck_proxy)
            g_warning ("Error getting ConsoleKit proxy: %s", error->message);
        g_clear_error (&error);
        if (!ck_proxy)
            return FALSE;
    }

    result = g_dbus_proxy_call_sync (ck_proxy,
                                     function,
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);

    if (!result)
        g_warning ("Error calling ConsoleKit function %s: %s", function, error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(b)")))
        g_variant_get (result, "(b)", &function_result);

    g_variant_unref (result);
    return function_result;
}

/**
 * lightdm_get_can_restart:
 *
 * Checks if is authorized to do a system restart.
 *
 * Return value: #TRUE if can restart the system
 **/
gboolean
lightdm_get_can_restart (void)
{
    return ck_call_function ("CanRestart", TRUE);
}

/**
 * lightdm_restart:
 *
 * Triggers a system restart.
 **/
void
lightdm_restart (void)
{
    ck_call_function ("Restart", FALSE);
}

/**
 * lightdm_get_can_shutdown:
 *
 * Checks if is authorized to do a system shutdown.
 *
 * Return value: #TRUE if can shutdown the system
 **/
gboolean
lightdm_get_can_shutdown (void)
{
    return ck_call_function ("CanStop", TRUE);
}

/**
 * lightdm_shutdown:
 *
 * Triggers a system shutdown.
 **/
void
lightdm_shutdown (void)
{
    ck_call_function ("Stop", FALSE);
}
