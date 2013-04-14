/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include <config.h>

#include <string.h>
#include <gio/gio.h>

#include "lightdm/power.h"

static GDBusProxy *upower_proxy = NULL;
static GDBusProxy *ck_proxy = NULL;
static GDBusProxy *login1_proxy = NULL;

static gboolean
upower_call_function (const gchar *function, gboolean default_result, GError **error)
{
    GVariant *result;
    gboolean function_result = FALSE;

    if (!upower_proxy)
    {
        upower_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                      G_DBUS_PROXY_FLAGS_NONE,
                                                      NULL,
                                                      "org.freedesktop.UPower",
                                                      "/org/freedesktop/UPower",
                                                      "org.freedesktop.UPower",
                                                      NULL,
                                                      error);
        if (!upower_proxy)
            return FALSE;
    }

    result = g_dbus_proxy_call_sync (upower_proxy,
                                     function,
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     error);
    if (!result)
        return default_result;

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
    return upower_call_function ("SuspendAllowed", FALSE, NULL);
}

/**
 * lightdm_suspend:
 * @error: return location for a #GError, or %NULL
 *
 * Triggers a system suspend.
 * 
 * Return value: #TRUE if suspend initiated.
 **/
gboolean
lightdm_suspend (GError **error)
{
    return upower_call_function ("Suspend", TRUE, error);
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
    return upower_call_function ("HibernateAllowed", FALSE, NULL);
}

/**
 * lightdm_hibernate:
 * @error: return location for a #GError, or %NULL
 *
 * Triggers a system hibernate.
 * 
 * Return value: #TRUE if hibernate initiated.
 **/
gboolean
lightdm_hibernate (GError **error)
{
    return upower_call_function ("Hibernate", TRUE, error);
}

static gboolean
ck_call_function (const gchar *function, gboolean default_result, GError **error)
{
    GVariant *result;
    gboolean function_result = FALSE;

    if (!ck_proxy)
    {
        ck_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  NULL,
                                                  "org.freedesktop.ConsoleKit",
                                                  "/org/freedesktop/ConsoleKit/Manager",
                                                  "org.freedesktop.ConsoleKit.Manager",
                                                  NULL,
                                                  error);
        if (!ck_proxy)
            return FALSE;
    }

    result = g_dbus_proxy_call_sync (ck_proxy,
                                     function,
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     error);

    if (!result)
        return default_result;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(b)")))
        g_variant_get (result, "(b)", &function_result);

    g_variant_unref (result);
    return function_result;
}

static gboolean
login1_call_function (const gchar *function, GVariant *parameters, gboolean default_result, GError **error)
{
    GVariant *result;
    gboolean function_result = FALSE;
    gchar *str_result;

    if (!login1_proxy)
    {
        login1_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                      G_DBUS_PROXY_FLAGS_NONE,
                                                      NULL,
                                                      "org.freedesktop.login1",
                                                      "/org/freedesktop/login1",
                                                      "org.freedesktop.login1.Manager",
                                                      NULL,
                                                      error);
        if (!login1_proxy)
            return FALSE;
    }

    result = g_dbus_proxy_call_sync (login1_proxy,
                                     function,
                                     parameters,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     error);

    if (!result)
        return default_result;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(b)")))
        g_variant_get (result, "(s)", &function_result);

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(s)")))
    {
        g_variant_get (result, "(b)", str_result);
        if (g_strcmp0 (str_result, "yes") == 0)
            function_result = TRUE;
        else
            function_result = default_result;
    }       
    
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
    gboolean function_result;

    function_result = login1_call_function ("CanReboot", NULL, FALSE, NULL);
    if (!function_result)
        function_result = ck_call_function ("CanRestart", FALSE, NULL);

    return function_result;
}

/**
 * lightdm_restart:
 * @error: return location for a #GError, or %NULL
 *
 * Triggers a system restart.
 *
 * Return value: #TRUE if restart initiated.
 **/
gboolean
lightdm_restart (GError **error)
{
    gboolean function_result;

    function_result = login1_call_function ("Reboot", g_variant_new("(b)",0), TRUE, error);
    if (!function_result)
        function_result = ck_call_function ("Restart", TRUE, error);

    return function_result;
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
    gboolean function_result;

    function_result = login1_call_function ("CanPowerOff", NULL, FALSE, NULL);
    if (!function_result)
        function_result = ck_call_function ("CanStop", FALSE, NULL);

    return function_result;
}

/**
 * lightdm_shutdown:
 * @error: return location for a #GError, or %NULL
 *
 * Triggers a system shutdown.
 * 
 * Return value: #TRUE if shutdown initiated.
 **/
gboolean
lightdm_shutdown (GError **error)
{
    gboolean function_result;

    function_result = login1_call_function ("PowerOff", g_variant_new("(b)",0), TRUE, error);
    if (!function_result)
          function_result = ck_call_function ("Stop", TRUE, error);

    return function_result;
}
