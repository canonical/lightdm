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
    return g_key_file_load_from_file (config->priv->key_file, path, G_KEY_FILE_NONE, error);
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
