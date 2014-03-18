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

#include <string.h>

#include "configuration.h"

struct ConfigurationPrivate
{
    GKeyFile *key_file;
};

G_DEFINE_TYPE (Configuration, config, G_TYPE_OBJECT);

static Configuration *configuration_instance = NULL;

Configuration *
config_get_instance (void)
{
    if (!configuration_instance)
        configuration_instance = g_object_new (CONFIGURATION_TYPE, NULL);
    return configuration_instance;
}

gboolean
config_load_from_file (Configuration *config, const gchar *path, GError **error)
{
    GKeyFile *key_file;
    gchar **groups;
    int i;

    key_file = g_key_file_new ();
    if (!g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, error))
    {
        g_key_file_free (key_file);
        return FALSE;
    }

    groups = g_key_file_get_groups (key_file, NULL);
    for (i = 0; groups[i]; i++)
    {
        gchar **keys;
        int j;

        keys = g_key_file_get_keys (key_file, groups[i], NULL, error);
        if (!keys)
            break;
      
        for (j = 0; keys[j]; j++)
        {
            gchar *value;

            value = g_key_file_get_value (key_file, groups[i], keys[j], NULL);
            g_key_file_set_value (config->priv->key_file, groups[i], keys[j], value);
            g_free (value);
        }

        g_strfreev (keys);
    }
    g_strfreev (groups);

    g_key_file_free (key_file);

    return TRUE;
}

static gchar *
path_make_absolute (gchar *path)
{
    gchar *cwd, *abs_path;

    if (!path)
        return NULL;

    if (g_path_is_absolute (path))
        return path;

    cwd = g_get_current_dir ();
    abs_path = g_build_filename (cwd, path, NULL);
    g_free (path);

    return abs_path;
}

static int
compare_strings (gconstpointer a, gconstpointer b)
{
    return strcmp (a, b);
}

static void
load_config_directory (const gchar *path, GList **messages)
{
    GDir *dir;
    GList *files = NULL, *link;
    GError *error = NULL;

    /* Find configuration files */
    dir = g_dir_open (path, 0, &error);
    if (error && !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_printerr ("Failed to open configuration directory %s: %s\n", path, error->message);
    g_clear_error (&error);
    if (dir)
    {
        const gchar *name;
        while ((name = g_dir_read_name (dir)))
            files = g_list_append (files, g_strdup (name));
        g_dir_close (dir);
    }

    /* Sort alphabetically and load onto existing configuration */
    files = g_list_sort (files, compare_strings);
    for (link = files; link; link = link->next)
    {
        gchar *filename = link->data;
        gchar *conf_path;

        conf_path = g_build_filename (path, filename, NULL);
        if (g_str_has_suffix (filename, ".conf"))
        {
            if (messages)
                *messages = g_list_append (*messages, g_strdup_printf ("Loading configuration from %s", conf_path));
            config_load_from_file (config_get_instance (), conf_path, &error);
            if (error && !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
                g_printerr ("Failed to load configuration from %s: %s\n", filename, error->message);
            g_clear_error (&error);
        }
        else
            g_debug ("Ignoring configuration file %s, it does not have .conf suffix", conf_path);
        g_free (conf_path);
    }
    g_list_free_full (files, g_free);
}

gboolean
config_load_from_standard_locations (Configuration *config, const gchar *config_path, GList **messages)
{
    gchar *config_dir, *config_d_dir = NULL;
    gboolean explicit_config = FALSE;
    gboolean success = TRUE;
    GError *error = NULL;

    if (config_path)
    {
        config_dir = g_path_get_basename (config_path);
        config_dir = path_make_absolute (config_dir);
        explicit_config = TRUE;
    }
    else
    {
        config_dir = g_strdup (CONFIG_DIR);
        config_d_dir = g_build_filename (config_dir, "lightdm.conf.d", NULL);
        config_path = g_build_filename (config_dir, "lightdm.conf", NULL);
    }
    config_set_string (config, "LightDM", "config-directory", config_dir);
    g_free (config_dir);

    load_config_directory (SYSTEM_CONFIG_DIR, messages);
    if (config_d_dir)
        load_config_directory (config_d_dir, messages);
    g_free (config_d_dir);

    if (messages)
        *messages = g_list_append (*messages, g_strdup_printf ("Loading configuration from %s", config_path));
    if (!config_load_from_file (config, config_path, &error))
    {
        gboolean is_empty;

        is_empty = error && g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);

        if (explicit_config || !is_empty)
        {
            if (error)
                g_printerr ("Failed to load configuration from %s: %s\n", config_path, error->message);
            success = FALSE;
        }
    }
    g_clear_error (&error);

    return success;
}

gchar **
config_get_groups (Configuration *config)
{
    return g_key_file_get_groups (config->priv->key_file, NULL);
}

gchar **
config_get_keys (Configuration *config, const gchar *group_name)
{
    return g_key_file_get_keys (config->priv->key_file, group_name, NULL, NULL);
}

gboolean
config_has_key (Configuration *config, const gchar *section, const gchar *key)
{
    return g_key_file_has_key (config->priv->key_file, section, key, NULL);
}

void
config_set_string (Configuration *config, const gchar *section, const gchar *key, const gchar *value)
{
    g_key_file_set_string (config->priv->key_file, section, key, value);
}

gchar *
config_get_string (Configuration *config, const gchar *section, const gchar *key)
{
    return g_key_file_get_string (config->priv->key_file, section, key, NULL);
}

void
config_set_string_list (Configuration *config, const gchar *section, const gchar *key, const gchar **value, gsize length)
{
    g_key_file_set_string_list (config->priv->key_file, section, key, value, length);
}

gchar **
config_get_string_list (Configuration *config, const gchar *section, const gchar *key)
{
    return g_key_file_get_string_list (config->priv->key_file, section, key, NULL, NULL);
}

void
config_set_integer (Configuration *config, const gchar *section, const gchar *key, gint value)
{
    g_key_file_set_integer (config->priv->key_file, section, key, value);
}

gint
config_get_integer (Configuration *config, const gchar *section, const gchar *key)
{
    return g_key_file_get_integer (config->priv->key_file, section, key, NULL);
}

void
config_set_boolean (Configuration *config, const gchar *section, const gchar *key, gboolean value)
{
    g_key_file_set_boolean (config->priv->key_file, section, key, value);
}

gboolean
config_get_boolean (Configuration *config, const gchar *section, const gchar *key)
{
    return g_key_file_get_boolean (config->priv->key_file, section, key, NULL);
}

static void
config_init (Configuration *config)
{
    config->priv = G_TYPE_INSTANCE_GET_PRIVATE (config, CONFIGURATION_TYPE, ConfigurationPrivate);
    config->priv->key_file = g_key_file_new ();  
}

static void
config_finalize (GObject *object)
{
    Configuration *self;

    self = CONFIGURATION (object);

    g_key_file_free (self->priv->key_file);

    G_OBJECT_CLASS (config_parent_class)->finalize (object);  
}

static void
config_class_init (ConfigurationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = config_finalize;  

    g_type_class_add_private (klass, sizeof (ConfigurationPrivate));
}
