/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

#include "dmrc.h"
#include "configuration.h"

GKeyFile *
dmrc_load (const gchar *username)
{
    struct passwd *user_info;
    GKeyFile *dmrc_file;
    gchar *path, *filename, *cache_dir;
    gboolean have_dmrc;
    GError *error = NULL;

    user_info = getpwnam (username);
    if (!user_info)
    {
        if (errno != 0)
            g_warning ("Unable to get information on user %s: %s", username, strerror (errno));
        return NULL;
    }

    dmrc_file = g_key_file_new ();

    /* Load from the user directory, if this fails (e.g. the user directory
     * is not yet mounted) then load from the cache */
    path = g_build_filename (user_info->pw_dir, ".dmrc", NULL);
    have_dmrc = g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_free (path);

    if (have_dmrc)
        return dmrc_file;
  
    /* If no ~/.dmrc, then load from the cache */
    filename = g_strdup_printf ("%s.dmrc", user_info->pw_name);
    cache_dir = config_get_string (config_get_instance (), "LightDM", "cache-directory");
    path = g_build_filename (cache_dir, "dmrc", filename, NULL);
    g_free (filename);
    g_free (cache_dir);

    if (!g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_KEEP_COMMENTS, &error))
        g_warning ("Failed to load .dmrc file %s: %s", path, error->message);
    g_clear_error (&error);
    g_free (path);

    return dmrc_file;
}

void
dmrc_save (GKeyFile *dmrc_file, const gchar *username)
{
    struct passwd *user_info;
    gchar *path, *filename, *cache_dir, *dmrc_cache_dir;
    gchar *data;
    gsize length;

    user_info = getpwnam (username);
    if (!user_info)
    {
        if (errno != 0)
            g_warning ("Unable to get information on user %s: %s", username, strerror (errno));
        return;
    }
  
    data = g_key_file_to_data (dmrc_file, &length, NULL);

    /* Update the users .dmrc */
    if (user_info)
    {
        path = g_build_filename (user_info->pw_dir, ".dmrc", NULL);
        g_file_set_contents (path, data, length, NULL);
        if (chown (path, user_info->pw_uid, user_info->pw_gid) < 0)
            g_warning ("Error setting ownership on %s: %s", path, strerror (errno));
        g_free (path);
    }

    /* Update the .dmrc cache */
    cache_dir = config_get_string (config_get_instance (), "LightDM", "cache-directory");
    dmrc_cache_dir = g_build_filename (cache_dir, "dmrc", NULL);
    g_mkdir_with_parents (dmrc_cache_dir, 0700);

    filename = g_strdup_printf ("%s.dmrc", username);
    path = g_build_filename (dmrc_cache_dir, filename, NULL);
    g_file_set_contents (path, data, length, NULL);

    g_free (dmrc_cache_dir);
    g_free (path);
    g_free (filename);
}
