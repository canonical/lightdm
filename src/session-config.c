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

    /* Desktop names */
    gchar **desktop_names;

    /* Command to run */
    gchar *command;

    /* TRUE if can run a greeter inside the session */
    gboolean allow_greeter;
};

G_DEFINE_TYPE_WITH_PRIVATE (SessionConfig, session_config, G_TYPE_OBJECT)

SessionConfig *
session_config_new_from_file (const gchar *filename, const gchar *default_session_type, GError **error)
{
    g_autoptr(GKeyFile) desktop_file = g_key_file_new ();
    if (!g_key_file_load_from_file (desktop_file, filename, G_KEY_FILE_NONE, error))
        return NULL;
    g_autofree gchar *command = g_key_file_get_string (desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, NULL);
    if (!command)
    {
        g_set_error (error,
                     G_KEY_FILE_ERROR,
                     G_KEY_FILE_ERROR_KEY_NOT_FOUND,
                     "No Exec option in session file: %s", filename);
        return NULL;
    }

    g_autoptr(SessionConfig) config = g_object_new (SESSION_CONFIG_TYPE, NULL);
    config->priv->command = g_steal_pointer (&command);
    config->priv->session_type = g_key_file_get_string (desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-Session-Type", NULL);
    if (!config->priv->session_type)
        config->priv->session_type = g_strdup (default_session_type);

    config->priv->desktop_names = g_key_file_get_string_list (desktop_file, G_KEY_FILE_DESKTOP_GROUP, "DesktopNames", NULL, NULL);
    if (!config->priv->desktop_names)
    {
        gchar *name;

        name = g_key_file_get_string (desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-DesktopName", NULL);
        if (name)
        {
            config->priv->desktop_names = g_malloc (sizeof (gchar *) * 2);
            config->priv->desktop_names[0] = name;
            config->priv->desktop_names[1] = NULL;
        }
    }
    config->priv->allow_greeter = g_key_file_get_boolean (desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-Allow-Greeter", NULL);

    return g_steal_pointer (&config);
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

gchar **
session_config_get_desktop_names (SessionConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);
    return config->priv->desktop_names;
}

gboolean
session_config_get_allow_greeter (SessionConfig *config)
{
    g_return_val_if_fail (config != NULL, FALSE);
    return config->priv->allow_greeter;
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

    g_clear_pointer (&self->priv->session_type, g_free);
    g_clear_pointer (&self->priv->desktop_names, g_strfreev);
    g_clear_pointer (&self->priv->command, g_free);

    G_OBJECT_CLASS (session_config_parent_class)->finalize (object);
}

static void
session_config_class_init (SessionConfigClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = session_config_finalize;
}
