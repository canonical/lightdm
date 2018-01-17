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
#include "privileges.h"
#include "user-list.h"

GKeyFile *
dmrc_load (CommonUser *user)
{
    g_autoptr(GKeyFile) dmrc_file = g_key_file_new ();

    /* Load from the user directory, if this fails (e.g. the user directory
     * is not yet mounted) then load from the cache */
    g_autofree gchar *path = g_build_filename (common_user_get_home_directory (user), ".dmrc", NULL);

    /* Guard against privilege escalation through symlinks, etc. */
    gboolean drop_privileges = geteuid () == 0;
    if (drop_privileges)
        privileges_drop (common_user_get_uid (user), common_user_get_gid (user));
    gboolean have_dmrc = g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    if (drop_privileges)
        privileges_reclaim ();

    /* If no ~/.dmrc, then load from the cache */
    if (!have_dmrc)
    {
        g_autofree gchar *filename = g_strdup_printf ("%s.dmrc", common_user_get_name (user));
        g_autofree gchar *cache_dir = config_get_string (config_get_instance (), "LightDM", "cache-directory");
        g_autofree gchar *cache_path = g_build_filename (cache_dir, "dmrc", filename, NULL);

        g_key_file_load_from_file (dmrc_file, cache_path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    }

    return g_steal_pointer (&dmrc_file);
}

void
dmrc_save (GKeyFile *dmrc_file, CommonUser *user)
{
    gsize length;
    g_autofree gchar *data = g_key_file_to_data (dmrc_file, &length, NULL);

    /* Update the users .dmrc */
    g_autofree gchar *path = g_build_filename (common_user_get_home_directory (user), ".dmrc", NULL);

    /* Guard against privilege escalation through symlinks, etc. */
    gboolean drop_privileges = geteuid () == 0;
    if (drop_privileges)
        privileges_drop (common_user_get_uid (user), common_user_get_gid (user));
    g_debug ("Writing %s", path);
    g_file_set_contents (path, data, length, NULL);
    if (drop_privileges)
        privileges_reclaim ();

    /* Update the .dmrc cache */
    g_autofree gchar *cache_dir = config_get_string (config_get_instance (), "LightDM", "cache-directory");
    g_autofree gchar *dmrc_cache_dir = g_build_filename (cache_dir, "dmrc", NULL);
    if (g_mkdir_with_parents (dmrc_cache_dir, 0700) < 0)
        g_warning ("Failed to make DMRC cache directory %s: %s", dmrc_cache_dir, strerror (errno));

    g_autofree gchar *filename = g_strdup_printf ("%s.dmrc", common_user_get_name (user));
    g_autofree gchar *cache_path = g_build_filename (dmrc_cache_dir, filename, NULL);
    g_file_set_contents (cache_path, data, length, NULL);
}
