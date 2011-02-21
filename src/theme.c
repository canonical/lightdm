/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <config.h>

#include <string.h>

#include "theme.h"

GKeyFile *
load_theme (const gchar *name, GError **error)
{
    gchar *path;
    GKeyFile *theme;
    gboolean result;

    path = g_build_filename (THEME_DIR, name, "index.theme", NULL);

    g_debug ("Looking for %s theme in %s", name, path);

    theme = g_key_file_new ();
    result = g_key_file_load_from_file (theme, path, G_KEY_FILE_NONE, error);
    g_free (path);

    if (!result)
    {
        g_key_file_free (theme);
        return NULL;
    }

    path = g_build_filename (THEME_DIR, name, NULL);
    g_key_file_set_string (theme, "_", "path", path);
    g_free (path);

    return theme;
}

gchar *
theme_get_command (GKeyFile *theme)
{
    gchar *engine, *command;

    engine = g_key_file_get_value (theme, "theme", "engine", NULL);
    if (!engine)
    {
        g_warning ("No engine defined in theme");
        return NULL;
    }

    /* FIXME: This is perhaps unsafe - 'engine' could contain a relative path, e.g. "../../../run_something_malicious".
     * Perhaps there should be a check for this or the engines need a file like /usr/share/lightdm/engines/foo.engine */
    command = g_build_filename (THEME_ENGINE_DIR, engine, NULL);
    g_free (engine);

    return command;
}
