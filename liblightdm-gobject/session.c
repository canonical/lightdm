/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#include <string.h>
#include <gio/gdesktopappinfo.h>

#include "lightdm/session.h"

enum {
    PROP_0,
    PROP_KEY,
    PROP_NAME,
    PROP_COMMENT
};

typedef struct
{
    gchar *key;
    gchar *name;
    gchar *comment;
} LightDMSessionPrivate;

G_DEFINE_TYPE (LightDMSession, lightdm_session, G_TYPE_OBJECT);

#define GET_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_SESSION, LightDMSessionPrivate)

static gboolean have_sessions = FALSE;
static GList *sessions = NULL;

static void
update_sessions (void)
{
    GDir *directory;
    GError *error = NULL;

    if (have_sessions)
        return;

    directory = g_dir_open (XSESSIONS_DIR, 0, &error);
    if (!directory)
        g_warning ("Failed to open sessions directory: %s", error->message);
    g_clear_error (&error);
    if (!directory)
        return;

    while (TRUE)
    {
        const gchar *filename;
        GKeyFile *key_file;
        gchar *key, *path;
        gboolean result;

        filename = g_dir_read_name (directory);
        if (filename == NULL)
            break;

        if (!g_str_has_suffix (filename, ".desktop"))
            continue;

        key = g_strndup (filename, strlen (filename) - strlen (".desktop"));
        path = g_build_filename (XSESSIONS_DIR, filename, NULL);
        g_debug ("Loading session %s", path);

        key_file = g_key_file_new ();
        result = g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &error);
        if (!result)
            g_warning ("Failed to load session file %s: %s:", path, error->message);
        g_clear_error (&error);

        if (result && !g_key_file_get_boolean (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, NULL))
        {
            gchar *domain, *name, *comment;

#ifdef G_KEY_FILE_DESKTOP_KEY_GETTEXT_DOMAIN
            domain = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_GETTEXT_DOMAIN, NULL);
#else
            domain = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP, "X-GNOME-Gettext-Domain", NULL);
#endif
            name = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, domain, NULL);
            comment = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_COMMENT, domain, NULL);
            if (!comment)
                comment = g_strdup ("");
            if (name)
            {
                g_debug ("Loaded session %s (%s, %s)", key, name, comment);
                sessions = g_list_append (sessions, g_object_new (LIGHTDM_TYPE_SESSION, "key", key, "name", name, "comment", comment, NULL));
            }
            else
                g_warning ("Invalid session %s: %s", path, error->message);
            g_free (domain);
            g_free (name);
            g_free (comment);
        }

        g_free (key);
        g_free (path);
        g_key_file_free (key_file);
    }

    g_dir_close (directory);

    have_sessions = TRUE;
}

/**
 * lightdm_get_sessions:
 *
 * Get the available sessions.
 *
 * Return value: (element-type LightDMSession) (transfer none): A list of #LightDMSession
 **/
GList *
lightdm_get_sessions (void)
{
    update_sessions ();
    return sessions;
}

/**
 * lightdm_session_get_key
 * @session: A #LightDMSession
 * 
 * Get the key for a session
 * 
 * Return value: The session key
 **/
const gchar *
lightdm_session_get_key (LightDMSession *session)
{
    g_return_val_if_fail (LIGHTDM_IS_SESSION (session), NULL);
    return GET_PRIVATE (session)->key;
}

/**
 * lightdm_session_get_name
 * @session: A #LightDMSession
 * 
 * Get the name for a session
 * 
 * Return value: The session name
 **/
const gchar *
lightdm_session_get_name (LightDMSession *session)
{
    g_return_val_if_fail (LIGHTDM_IS_SESSION (session), NULL);
    return GET_PRIVATE (session)->name;
}

/**
 * lightdm_session_get_comment
 * @session: A #LightDMSession
 * 
 * Get the comment for a session
 * 
 * Return value: The session comment
 **/
const gchar *
lightdm_session_get_comment (LightDMSession *session)
{
    g_return_val_if_fail (LIGHTDM_IS_SESSION (session), NULL);
    return GET_PRIVATE (session)->comment;
}

static void
lightdm_session_init (LightDMSession *session)
{
}

static void
lightdm_session_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LightDMSession *self = LIGHTDM_SESSION (object);
    LightDMSessionPrivate *priv = GET_PRIVATE (self);

    switch (prop_id) {
    case PROP_KEY:
        g_free (priv->key);
        priv->key = g_strdup (g_value_get_string (value));
        break;
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_strdup (g_value_get_string (value));
        break;
    case PROP_COMMENT:
        g_free (priv->comment);
        priv->comment = g_strdup (g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_session_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LightDMSession *self;

    self = LIGHTDM_SESSION (object);

    switch (prop_id) {
    case PROP_KEY:
        g_value_set_string (value, lightdm_session_get_key (self));
        break;
    case PROP_NAME:
        g_value_set_string (value, lightdm_session_get_name (self));
        break;
    case PROP_COMMENT:
        g_value_set_string (value, lightdm_session_get_comment (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_session_class_init (LightDMSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
    g_type_class_add_private (klass, sizeof (LightDMSessionPrivate));

    object_class->set_property = lightdm_session_set_property;
    object_class->get_property = lightdm_session_get_property;

    g_object_class_install_property(object_class,
                                    PROP_KEY,
                                    g_param_spec_string("key",
                                                        "key",
                                                        "Session key",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_NAME,
                                    g_param_spec_string("name",
                                                        "name",
                                                        "Session name",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_COMMENT,
                                    g_param_spec_string("comment",
                                                        "comment",
                                                        "Session comment",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
