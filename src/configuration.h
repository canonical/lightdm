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

#ifndef _CONFIGURATION_H_
#define _CONFIGURATION_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define CONFIGURATION_TYPE (config_get_type())
#define CONFIGURATION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), CONFIGURATION_TYPE, Configuration));

typedef struct ConfigurationPrivate ConfigurationPrivate;

typedef struct
{
    GObject             parent_instance;
    ConfigurationPrivate *priv;
} Configuration;

typedef struct
{
    GObjectClass parent_class;
} ConfigurationClass;

GType config_get_type (void);

Configuration *config_get_instance (void);

gboolean config_load_from_file (Configuration *config, const gchar *path, GError **error);

gchar **config_get_groups (Configuration *config);

gchar **config_get_keys (Configuration *config, const gchar *group_name);

gboolean config_has_key (Configuration *config, const gchar *section, const gchar *key);

void config_set_string (Configuration *config, const gchar *section, const gchar *key, const gchar *value);

gchar *config_get_string (Configuration *config, const gchar *section, const gchar *key);

void config_set_integer (Configuration *config, const gchar *section, const gchar *key, gint value);

gint config_get_integer (Configuration *config, const gchar *section, const gchar *key);

void config_set_boolean (Configuration *config, const gchar *section, const gchar *key, gboolean value);

gboolean config_get_boolean (Configuration *config, const gchar *section, const gchar *key);

G_END_DECLS

#endif /* _CONFIGURATION_H_ */
