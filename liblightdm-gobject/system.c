/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include <string.h>

#include "lightdm/system.h"

/**
 * SECTION:system
 * @title: System Information
 * @short_description: Get system information
 * @include: lightdm.h
 *
 * Helper functions to get system information.
 */

/**
 * lightdm_get_hostname:
 *
 * Return value: The name of the host we are running on.
 **/
const gchar *
lightdm_get_hostname (void)
{
    return g_get_host_name ();
}

static gboolean os_release_loaded = FALSE;
static gchar *os_id = NULL;
static gchar *os_name = NULL;
static gchar *os_version = NULL;
static gchar *os_version_id = NULL;
static gchar *os_pretty_name = NULL;

static void
use_os_value (const gchar *name, const gchar *value)
{
    if (strcmp (name, "ID") == 0)
        os_id = g_strdup (value);
    if (strcmp (name, "NAME") == 0)
        os_name = g_strdup (value);
    if (strcmp (name, "VERSION") == 0)
        os_version = g_strdup (value);
    if (strcmp (name, "VERSION_ID") == 0)
        os_version_id = g_strdup (value);
    if (strcmp (name, "PRETTY_NAME") == 0)
        os_pretty_name = g_strdup (value);
}

static void
load_os_release (void)
{
    if (os_release_loaded)
        return;

    g_autofree gchar *data = NULL;
    if (!g_file_get_contents ("/etc/os-release", &data, NULL, NULL))
        return;

    g_auto(GStrv) lines = g_strsplit (data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++)
    {
        g_auto(GStrv) tokens = g_strsplit (lines[i], "=", 2);
        if (tokens[0] != NULL && tokens[1] != NULL)
        {
            const gchar *name = g_strstrip (tokens[0]);
            gchar *value = g_strstrip (tokens[1]);
            size_t value_length = strlen (value);
            if (value_length > 1 && value[0] == '\"' && value[value_length - 1] == '\"')
            {
                 value[value_length - 1] = '\0';
                 value++;
                 use_os_value (name, value);
            }
        }
    }

    os_release_loaded = TRUE;
}

/**
 * lightdm_get_os_id:
 *
 * Get a word describing the OS, suitable for checking which OS the greeter is running on.
 * e.g. "ubuntu"
 *
 * Return value: (nullable): a string (ID variable from /etc/os-release) or %NULL if not set.
 **/
const gchar *
lightdm_get_os_id (void)
{
    load_os_release ();
    return os_id;
}

/**
 * lightdm_get_os_name:
 *
 * Get a line of text describing the OS without version information, suitable for presentation to the user.
 * e.g. "Ubuntu"
 *
 * Return value: (nullable): a string (NAME variable from /etc/os-release) or %NULL if not set.
 **/
const gchar *
lightdm_get_os_name (void)
{
    load_os_release ();
    return os_name;
}

/**
 * lightdm_get_os_pretty_name:
 *
 * Get a line of text describing the OS, suitable for presentation to the user.
 * e.g. "Ubuntu 16.04.1 LTS"
 *
 * Return value: (nullable): a string (PRETTY_NAME variable from /etc/os-release) or %NULL if not set.
 **/
const gchar *
lightdm_get_os_pretty_name (void)
{
    load_os_release ();
    return os_pretty_name;
}

/**
 * lightdm_get_os_version:
 *
 * Get a line of text describing the OS version, suitable for presentation to the user.
 * e.g. "16.04.1 LTS (Xenial Xapus)"
 *
 * Return value: (nullable): a string (VERSION variable from /etc/os-release) or %NULL if not set.
 **/
const gchar *
lightdm_get_os_version (void)
{
    load_os_release ();
    return os_version;
}

/**
 * lightdm_get_os_version_id:
 *
 * Get a word describing the OS version, suitable for checking which version of the OS this greeter is running on.
 * e.g. "16.04"
 *
 * Return value: (nullable): a string (VERSION_ID variable from /etc/os-release) or %NULL if not set.
 **/
const gchar *
lightdm_get_os_version_id (void)
{
    load_os_release ();
    return os_version_id;
}

/**
 * lightdm_get_motd:
 *
 * Get a system message that should be presented to the user.
 * e.g. "Welcome to Yoyodyne"
 *
 * Return value: (nullable): a string (the contents of /etc/motd) or %NULL if not set.
 **/
gchar *
lightdm_get_motd (void)
{
    gchar *data = NULL;
    g_file_get_contents ("/etc/motd", &data, NULL, NULL);
    return data;
}
