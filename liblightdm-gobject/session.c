/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include <string.h>
#include <gio/gdesktopappinfo.h>

#include "configuration.h"
#include "lightdm/session.h"

/**
 * SECTION:session
 * @short_description: Choose the session to use
 * @include: lightdm.h
 *
 * Object containing information about a session type. #LightDMSession objects are not created by the user, but provided by the #LightDMGreeter object.
 */

/**
 * LightDMSession:
 *
 * #LightDMSession is an opaque data structure and can only be accessed
 * using the provided functions.
 */

/**
 * LightDMSessionClass:
 *
 * Class structure for #LightDMSession.
 */

enum {
    PROP_KEY = 1,
    PROP_NAME,
    PROP_COMMENT
};

typedef struct
{
    gchar *key;
    gchar *type;
    gchar *name;
    gchar *comment;
} LightDMSessionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LightDMSession, lightdm_session, G_TYPE_OBJECT)

static gboolean have_sessions = FALSE;
static GList *local_sessions = NULL;
static GList *remote_sessions = NULL;

static gint
compare_session (gconstpointer a, gconstpointer b)
{
    return strcmp (lightdm_session_get_name (LIGHTDM_SESSION (a)), lightdm_session_get_name (LIGHTDM_SESSION (b)));
}

static LightDMSession *
load_session (GKeyFile *key_file, const gchar *key, const gchar *default_type)
{
    if (g_key_file_get_boolean (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, NULL) ||
        g_key_file_get_boolean (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL))
        return NULL;

#ifdef G_KEY_FILE_DESKTOP_KEY_GETTEXT_DOMAIN
    g_autofree gchar *domain = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_GETTEXT_DOMAIN, NULL);
#else
    g_autofree gchar *domain = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP, "X-GNOME-Gettext-Domain", NULL);
#endif
    g_autofree gchar *name = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, domain, NULL);
    if (!name)
    {
        g_warning ("Ignoring session without name");
        return NULL;
    }

    g_autofree gchar *try_exec = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_TRY_EXEC, domain, NULL);
    if (try_exec)
    {
        g_autofree gchar *full_path = g_find_program_in_path (try_exec);
        if (!full_path)
            return NULL;
    }

    g_autofree gchar *type = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-Session-Type", NULL);
    if (!type)
        type = strdup (default_type);

    LightDMSession *session = g_object_new (LIGHTDM_TYPE_SESSION, NULL);
    LightDMSessionPrivate *priv = lightdm_session_get_instance_private (session);

    g_free (priv->key);
    priv->key = g_strdup (key);

    g_free (priv->type);
    priv->type = g_steal_pointer (&type);

    g_free (priv->name);
    priv->name = g_steal_pointer (&name);

    g_free (priv->comment);
    priv->comment = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_COMMENT, domain, NULL);
    if (!priv->comment)
        priv->comment = g_strdup ("");

    return session;
}

static GList *
load_sessions_dir (GList *sessions, const gchar *sessions_dir, const gchar *default_type)
{
    g_autoptr(GError) error = NULL;
    GDir *directory = g_dir_open (sessions_dir, 0, &error);
    if (error && !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Failed to open sessions directory: %s", error->message);
    if (!directory)
        return sessions;

    while (TRUE)
    {
        const gchar *filename = g_dir_read_name (directory);
        if (filename == NULL)
            break;

        if (!g_str_has_suffix (filename, ".desktop"))
            continue;

        g_autofree gchar *path = g_build_filename (sessions_dir, filename, NULL);

        g_autoptr(GKeyFile) key_file = g_key_file_new ();
        g_autoptr(GError) e = NULL;
        gboolean result = g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &e);
        if (e)
            g_warning ("Failed to load session file %s: %s:", path, e->message);

        if (result)
        {
            g_autofree gchar *key = g_strndup (filename, strlen (filename) - strlen (".desktop"));
            LightDMSession *session = load_session (key_file, key, default_type);
            LightDMSessionPrivate *priv = lightdm_session_get_instance_private (session);
            if (session)
            {
                g_debug ("Loaded session %s (%s, %s)", path, priv->name, priv->comment);
                sessions = g_list_insert_sorted (sessions, session, compare_session);
            }
            else
                g_debug ("Ignoring session %s", path);
        }
    }

    g_dir_close (directory);

    return sessions;
}

static GList *
load_sessions (const gchar *sessions_dir)
{
    g_auto(GStrv) dirs = g_strsplit (sessions_dir, ":", -1);
    GList *sessions = NULL;
    for (int i = 0; dirs[i]; i++)
    {
        const gchar *default_type = "x";

        if (dirs[i] != NULL && g_str_has_suffix (dirs[i], "/wayland-sessions") == TRUE)
            default_type = "wayland";

        sessions = load_sessions_dir (sessions, dirs[i], default_type);
    }

    return sessions;
}

static void
update_sessions (void)
{
    if (have_sessions)
        return;

    g_autofree gchar *sessions_dir = g_strdup (SESSIONS_DIR);
    g_autofree gchar *remote_sessions_dir = g_strdup (REMOTE_SESSIONS_DIR);

    /* Use session directory from configuration */
    config_load_from_standard_locations (config_get_instance (), NULL, NULL);

    gchar *value = config_get_string (config_get_instance (), "LightDM", "sessions-directory");
    if (value)
    {
        g_free (sessions_dir);
        sessions_dir = value;
    }

    value = config_get_string (config_get_instance (), "LightDM", "remote-sessions-directory");
    if (value)
    {
        g_free (remote_sessions_dir);
        remote_sessions_dir = value;
    }

    local_sessions = load_sessions (sessions_dir);
    remote_sessions = load_sessions (remote_sessions_dir);

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
    return local_sessions;
}

/**
 * lightdm_get_remote_sessions:
 *
 * Get the available remote sessions.
 *
 * Return value: (element-type LightDMSession) (transfer none): A list of #LightDMSession
 **/
GList *
lightdm_get_remote_sessions (void)
{
    update_sessions ();
    return remote_sessions;
}

/**
 * lightdm_session_get_key:
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

    LightDMSessionPrivate *priv = lightdm_session_get_instance_private (session);
    return priv->key;
}

/**
 * lightdm_session_get_session_type:
 * @session: A #LightDMSession
 *
 * Get the type a session
 *
 * Return value: The session type, e.g. x or mir
 **/
const gchar *
lightdm_session_get_session_type (LightDMSession *session)
{
    g_return_val_if_fail (LIGHTDM_IS_SESSION (session), NULL);

    LightDMSessionPrivate *priv = lightdm_session_get_instance_private (session);
    return priv->type;
}

/**
 * lightdm_session_get_name:
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

    LightDMSessionPrivate *priv = lightdm_session_get_instance_private (session);
    return priv->name;
}

/**
 * lightdm_session_get_comment:
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

    LightDMSessionPrivate *priv = lightdm_session_get_instance_private (session);
    return priv->comment;
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
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lightdm_session_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LightDMSession *self = LIGHTDM_SESSION (object);

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
lightdm_session_finalize (GObject *object)
{
    LightDMSession *self = LIGHTDM_SESSION (object);
    LightDMSessionPrivate *priv = lightdm_session_get_instance_private (self);

    g_free (priv->key);
    g_free (priv->type);
    g_free (priv->name);
    g_free (priv->comment);
}

static void
lightdm_session_class_init (LightDMSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lightdm_session_set_property;
    object_class->get_property = lightdm_session_get_property;
    object_class->finalize = lightdm_session_finalize;

    g_object_class_install_property (object_class,
                                     PROP_KEY,
                                     g_param_spec_string ("key",
                                                          "key",
                                                          "Session key",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_NAME,
                                     g_param_spec_string ("name",
                                                          "name",
                                                          "Session name",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_COMMENT,
                                     g_param_spec_string ("comment",
                                                          "comment",
                                                          "Session comment",
                                                          NULL,
                                                          G_PARAM_READABLE));
}
