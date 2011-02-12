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
    gchar *engine, *command = NULL;

    engine = g_key_file_get_value (theme, "theme", "engine", NULL);
    if (!engine)
    {
        g_warning ("No engine defined in theme");
        return NULL;
    }

    if (strcmp (engine, "gtk") == 0)
        command = g_build_filename (THEME_ENGINE_DIR, "lightdm-gtk-greeter", NULL);
    else if (strcmp (engine, "webkit") == 0)
    {
        gchar *binary, *url;

        binary = g_build_filename (THEME_ENGINE_DIR, "lightdm-webkit-greeter", NULL);
        url = g_key_file_get_value (theme, "theme", "url", NULL);
        if (url)
        {
            if (strchr (url, ':'))
                command = g_strdup_printf ("%s %s", binary, url);
            else
                command = g_strdup_printf ("%s file://%s/%s", binary, g_key_file_get_value (theme, "_", "path", NULL), url);
        }
        else
            g_warning ("Missing URL in WebKit theme");
        g_free (binary);
        g_free (url);
    }
    else if (strcmp (engine, "custom") == 0)
    {
        command = g_key_file_get_value (theme, "theme", "command", NULL);
        if (!command)
            g_warning ("Missing command in custom theme");
    }
    else
        g_warning ("Unknown theme engine: %s", engine);

    return command;
}
