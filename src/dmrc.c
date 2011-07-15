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
#include <unistd.h>

#include "dmrc.h"
#include "configuration.h"
#include "user.h"

GKeyFile *
dmrc_load (const gchar *username)
{
    User *user;
    GKeyFile *dmrc_file;
    gchar *path;
    gboolean have_dmrc;

    dmrc_file = g_key_file_new ();

    user = user_get_by_name (username);
    if (!user)
    {
        g_warning ("Cannot load .dmrc file, unable to get information on user %s", username);      
        return dmrc_file;
    }

    /* Load from the user directory, if this fails (e.g. the user directory
     * is not yet mounted) then load from the cache */
    path = g_build_filename (user_get_home_directory (user), ".dmrc", NULL);
    have_dmrc = g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_free (path);

    /* If no ~/.dmrc, then load from the cache */  
    if (!have_dmrc)
    {
        gchar *filename, *cache_dir;

        filename = g_strdup_printf ("%s.dmrc", user_get_name (user));
        cache_dir = config_get_string (config_get_instance (), "Directories", "cache-directory");
        path = g_build_filename (cache_dir, "dmrc", filename, NULL);
        g_free (filename);
        g_free (cache_dir);

        g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
        g_free (path);
    }

    g_object_unref (user);

    return dmrc_file;
}

void
dmrc_save (GKeyFile *dmrc_file, const gchar *username)
{
    User *user;
    gchar *path, *filename, *cache_dir, *dmrc_cache_dir;
    gchar *data;
    gsize length;

    user = user_get_by_name (username);
    if (!user)
    {
        g_warning ("Not saving DMRC file - unable to get information on user %s", username);
        return;
    }

    data = g_key_file_to_data (dmrc_file, &length, NULL);

    /* Update the users .dmrc */
    if (user)
    {
        path = g_build_filename (user_get_home_directory (user), ".dmrc", NULL);
        g_file_set_contents (path, data, length, NULL);
        if (getuid () == 0 && chown (path, user_get_uid (user), user_get_gid (user)) < 0)
            g_warning ("Error setting ownership on %s: %s", path, strerror (errno));
        g_free (path);
    }

    /* Update the .dmrc cache */
    cache_dir = config_get_string (config_get_instance (), "Directories", "cache-directory");
    dmrc_cache_dir = g_build_filename (cache_dir, "dmrc", NULL);
    g_mkdir_with_parents (dmrc_cache_dir, 0700);

    filename = g_strdup_printf ("%s.dmrc", username);
    path = g_build_filename (dmrc_cache_dir, filename, NULL);
    g_file_set_contents (path, data, length, NULL);

    g_free (dmrc_cache_dir);
    g_free (path);
    g_free (filename);
    g_object_unref (user);
}
