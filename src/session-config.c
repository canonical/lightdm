/*
 * Copyright (C) 2013 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include "session-config.h"

struct SessionConfigPrivate
{
    /* Session type */
    gchar *session_type;

    /* Desktop name */
    gchar *desktop_name;

    /* Command to run */
    gchar *command;
};

G_DEFINE_TYPE (SessionConfig, session_config, G_TYPE_OBJECT);

SessionConfig *
session_config_new_from_file (const gchar *filename, GError **error)
{
    GKeyFile *desktop_file;
    SessionConfig *config;
    gchar *command;

    desktop_file = g_key_file_new ();
    if (!g_key_file_load_from_file (desktop_file, filename, G_KEY_FILE_NONE, error))
        return NULL;
    command = g_key_file_get_string (desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
    if (!command)
    {
        g_set_error (error,
                     G_KEY_FILE_ERROR,
                     G_KEY_FILE_ERROR_KEY_NOT_FOUND,
                     "No Exec option in session file: %s", filename);
        return NULL;
    }

    config = g_object_new (SESSION_CONFIG_TYPE, NULL);
    config->priv->command = command;
    config->priv->session_type = g_key_file_get_string (desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-Session-Type", NULL);
    if (!config->priv->session_type)
        config->priv->session_type = g_strdup ("x");
    config->priv->desktop_name = g_key_file_get_string (desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-DesktopName", NULL);

    g_key_file_free (desktop_file);

    return config;
}

const gchar *
session_config_get_command (SessionConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);
    return config->priv->command;
}

const gchar *
session_config_get_session_type (SessionConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);
    return config->priv->session_type;
}

const gchar *
session_config_get_desktop_name (SessionConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);
    return config->priv->desktop_name;
}

static void
session_config_init (SessionConfig *config)
{
    config->priv = G_TYPE_INSTANCE_GET_PRIVATE (config, SESSION_CONFIG_TYPE, SessionConfigPrivate);
}

static void
session_config_finalize (GObject *object)
{
    SessionConfig *self = SESSION_CONFIG (object);

    g_free (self->priv->session_type);
    g_free (self->priv->desktop_name);
    g_free (self->priv->command);

    G_OBJECT_CLASS (session_config_parent_class)->finalize (object);
}

static void
session_config_class_init (SessionConfigClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = session_config_finalize;

    g_type_class_add_private (klass, sizeof (SessionConfigPrivate));
}
