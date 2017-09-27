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
    gchar *dir;
    GKeyFile *key_file;
    GList *sources;
    GHashTable *key_sources;
    GHashTable *lightdm_keys;
    GHashTable *seat_keys;
    GHashTable *xdmcp_keys;  
    GHashTable *vnc_keys;
};

typedef enum 
{
    KEY_UNKNOWN,
    KEY_SUPPORTED,
    KEY_DEPRECATED
} KeyStatus;

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
config_load_from_file (Configuration *config, const gchar *path, GList **messages, GError **error)
{
    g_autoptr(GKeyFile) key_file = NULL;
    gchar *source_path;
    g_auto(GStrv) groups = NULL;
    int i;

    key_file = g_key_file_new ();
    if (!g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, error))
        return FALSE;

    source_path = g_strdup (path);
    config->priv->sources = g_list_append (config->priv->sources, source_path);

    groups = g_key_file_get_groups (key_file, NULL);
    for (i = 0; groups[i]; i++)
    {
        g_auto(GStrv) keys = NULL;
        const gchar *group;
        GHashTable *known_keys = NULL;
        int j;

        /* Move keys from deprecated [SeatDefaults] into [Seat:*] */
        group = groups[i];
        if (strcmp (group, "SeatDefaults") == 0)
        {
            if (messages)
                *messages = g_list_append (*messages, g_strdup ("  [SeatDefaults] is now called [Seat:*], please update this configuration"));
            group = "Seat:*";
        }

        /* Check if we know this group */
        if (strcmp (group, "LightDM") == 0)
            known_keys = config->priv->lightdm_keys;      
        else if (g_str_has_prefix (group, "Seat:"))
            known_keys = config->priv->seat_keys;
        else if (strcmp (group, "XDMCPServer") == 0)
            known_keys = config->priv->xdmcp_keys;
        else if (strcmp (group, "VNCServer") == 0)
            known_keys = config->priv->vnc_keys;
        else if (messages)
            *messages = g_list_append (*messages, g_strdup_printf ("  Unknown group [%s] in configuration", group));

        keys = g_key_file_get_keys (key_file, groups[i], NULL, error);
        if (!keys)
            break;

        for (j = 0; keys[j]; j++)
        {
            g_autofree gchar *value = NULL;
            g_autofree gchar *k = NULL;

            if (known_keys != NULL)
            {
                KeyStatus status;

                status = GPOINTER_TO_INT (g_hash_table_lookup (known_keys, keys[j]));
                if (status == KEY_UNKNOWN) {
                    if (messages != NULL)
                        *messages = g_list_append (*messages, g_strdup_printf ("  [%s] contains unknown option %s", group, keys[j]));
                }
                else if (status == KEY_DEPRECATED) {
                    if (messages != NULL)
                        *messages = g_list_append (*messages, g_strdup_printf ("  [%s] contains deprecated option %s, this can be safely removed", group, keys[j]));
                }
            }

            value = g_key_file_get_value (key_file, groups[i], keys[j], NULL);
            g_key_file_set_value (config->priv->key_file, group, keys[j], value);

            k = g_strdup_printf ("%s]%s", group, keys[j]);
            g_hash_table_insert (config->priv->key_sources, g_steal_pointer (&k), source_path);
        }
    }

    return TRUE;
}

static gchar *
path_make_absolute (gchar *path)
{
    g_autofree gchar *cwd = NULL;

    if (!path)
        return NULL;

    if (g_path_is_absolute (path))
        return path;

    cwd = g_get_current_dir ();
    return g_build_filename (cwd, path, NULL);
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
    g_autoptr(GError) error = NULL;

    /* Find configuration files */
    dir = g_dir_open (path, 0, &error);
    if (error && !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_printerr ("Failed to open configuration directory %s: %s\n", path, error->message);
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
        g_autofree gchar *conf_path = NULL;
        g_autoptr(GError) conf_error = NULL;

        conf_path = g_build_filename (path, filename, NULL);
        if (g_str_has_suffix (filename, ".conf"))
        {
            if (messages)
                *messages = g_list_append (*messages, g_strdup_printf ("Loading configuration from %s", conf_path));
            config_load_from_file (config_get_instance (), conf_path, messages, &conf_error);
            if (conf_error && !g_error_matches (conf_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
                g_printerr ("Failed to load configuration from %s: %s\n", filename, conf_error->message);
        }
        else
            g_debug ("Ignoring configuration file %s, it does not have .conf suffix", conf_path);
    }
    g_list_free_full (files, g_free);
}

static void
load_config_directories (const gchar * const *dirs, GList **messages)
{
    gint i;

    /* Load in reverse order, because XDG_* fields are preference-ordered and the directories in front should override directories in back. */
    for (i = g_strv_length ((gchar **)dirs) - 1; i >= 0; i--)
    {
        g_autofree gchar *full_dir = g_build_filename (dirs[i], "lightdm", "lightdm.conf.d", NULL);
        if (messages)
            *messages = g_list_append (*messages, g_strdup_printf ("Loading configuration dirs from %s", full_dir));
        load_config_directory (full_dir, messages);
    }
}

gboolean
config_load_from_standard_locations (Configuration *config, const gchar *config_path, GList **messages)
{
    g_autofree gchar *config_d_dir = NULL;
    g_autofree gchar *path = NULL;
    g_autoptr(GError) error = NULL;

    g_return_val_if_fail (config->priv->dir == NULL, FALSE);

    load_config_directories (g_get_system_data_dirs (), messages);
    load_config_directories (g_get_system_config_dirs (), messages);

    if (config_path)
    {
        g_autofree gchar *basename = NULL;

        path = g_strdup (config_path);
        basename = g_path_get_basename (config_path);
        config->priv->dir = path_make_absolute (basename);
    }
    else
    {
        config->priv->dir = g_strdup (CONFIG_DIR);
        config_d_dir = g_build_filename (config->priv->dir, "lightdm.conf.d", NULL);
        path = g_build_filename (config->priv->dir, "lightdm.conf", NULL);
    }

    if (config_d_dir)
        load_config_directory (config_d_dir, messages);

    if (messages)
        *messages = g_list_append (*messages, g_strdup_printf ("Loading configuration from %s", path));
    if (!config_load_from_file (config, path, messages, &error))
    {
        gboolean is_empty;

        is_empty = error && g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);

        if (config_path || !is_empty)
        {
            if (error)
                g_printerr ("Failed to load configuration from %s: %s\n", path, error->message);
            return FALSE;
        }
    }

    return TRUE;
}

const gchar *
config_get_directory (Configuration *config)
{
    return config->priv->dir;
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

GList *
config_get_sources (Configuration *config)
{
    return config->priv->sources;
}

const gchar *
config_get_source (Configuration *config, const gchar *section, const gchar *key)
{
    g_autofree gchar *k = NULL;
    const gchar *source;

    k = g_strdup_printf ("%s]%s", section, key);
    source = g_hash_table_lookup (config->priv->key_sources, k);

    return source;
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
    /* We don't use the standard function because it doesn't work with trailing whitespace:
     * https://bugzilla.gnome.org/show_bug.cgi?id=664740
     */
    /*return g_key_file_get_boolean (config->priv->key_file, section, key, NULL);*/

    g_autofree gchar *value = NULL;

    value = g_key_file_get_value (config->priv->key_file, section, key, NULL);
    if (!value)
        return FALSE;
    g_strchomp (value);
    return strcmp (value, "true") == 0;
}

static void
config_init (Configuration *config)
{
    config->priv = G_TYPE_INSTANCE_GET_PRIVATE (config, CONFIGURATION_TYPE, ConfigurationPrivate);
    config->priv->key_file = g_key_file_new ();
    config->priv->key_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    config->priv->lightdm_keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
    config->priv->seat_keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
    config->priv->xdmcp_keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
    config->priv->vnc_keys = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

    /* Build up tables of known keys */
    g_hash_table_insert (config->priv->lightdm_keys, "start-default-seat", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "greeter-user", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "minimum-display-number", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "minimum-vt", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "lock-memory", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "user-authority-in-system-dir", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "guest-account-script", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "logind-check-graphical", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "log-directory", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "run-directory", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "cache-directory", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "sessions-directory", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "remote-sessions-directory", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "greeters-directory", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "backup-logs", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "dbus-service", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->lightdm_keys, "logind-load-seats", GINT_TO_POINTER (KEY_DEPRECATED));

    g_hash_table_insert (config->priv->seat_keys, "type", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "pam-service", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "pam-autologin-service", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "pam-greeter-service", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xserver-backend", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xserver-command", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xmir-command", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xserver-config", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xserver-layout", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xserver-allow-tcp", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xserver-share", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xserver-hostname", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xserver-display-number", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xdmcp-manager", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xdmcp-port", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xdmcp-key", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "unity-compositor-command", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "unity-compositor-timeout", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "greeter-session", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "greeter-hide-users", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "greeter-allow-guest", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "greeter-show-manual-login", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "greeter-show-remote-login", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "user-session", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "allow-user-switching", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "allow-guest", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "guest-session", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "session-wrapper", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "greeter-wrapper", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "guest-wrapper", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "display-setup-script", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "display-stopped-script", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "greeter-setup-script", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "session-setup-script", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "session-cleanup-script", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "autologin-guest", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "autologin-user", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "autologin-user-timeout", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "autologin-in-background", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "autologin-session", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "exit-on-failure", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->seat_keys, "xdg-seat", GINT_TO_POINTER (KEY_DEPRECATED));

    g_hash_table_insert (config->priv->xdmcp_keys, "enabled", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->xdmcp_keys, "port", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->xdmcp_keys, "listen-address", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->xdmcp_keys, "key", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->xdmcp_keys, "hostname", GINT_TO_POINTER (KEY_SUPPORTED));

    g_hash_table_insert (config->priv->vnc_keys, "enabled", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->vnc_keys, "command", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->vnc_keys, "port", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->vnc_keys, "listen-address", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->vnc_keys, "width", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->vnc_keys, "height", GINT_TO_POINTER (KEY_SUPPORTED));
    g_hash_table_insert (config->priv->vnc_keys, "depth", GINT_TO_POINTER (KEY_SUPPORTED));  
}

static void
config_finalize (GObject *object)
{
    Configuration *self = CONFIGURATION (object);

    g_clear_pointer (&self->priv->dir, g_free);
    g_clear_pointer (&self->priv->key_file, g_key_file_free);
    g_list_free_full (self->priv->sources, g_free);
    g_hash_table_destroy (self->priv->key_sources);
    g_hash_table_destroy (self->priv->lightdm_keys);
    g_hash_table_destroy (self->priv->seat_keys);
    g_hash_table_destroy (self->priv->xdmcp_keys);
    g_hash_table_destroy (self->priv->vnc_keys);

    G_OBJECT_CLASS (config_parent_class)->finalize (object);  
}

static void
config_class_init (ConfigurationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = config_finalize;  

    g_type_class_add_private (klass, sizeof (ConfigurationPrivate));
}
