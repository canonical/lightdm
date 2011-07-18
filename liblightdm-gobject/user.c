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

#include "user-private.h"

enum {
    PROP_0,
    PROP_GREETER,
    PROP_NAME,
    PROP_REAL_NAME,
    PROP_DISPLAY_NAME,
    PROP_HOME_DIRECTORY,
    PROP_IMAGE,
    PROP_LANGUAGE,
    PROP_LAYOUT,
    PROP_SESSION,
    PROP_LOGGED_IN
};

typedef struct
{
    LdmGreeter *greeter;

    gchar *name;
    gchar *real_name;
    gchar *home_directory;
    gchar *image;
    gboolean logged_in;

    GKeyFile *dmrc_file;
    gchar *language;
    gchar *layout;
    gchar *session;
} LdmUserPrivate;

G_DEFINE_TYPE (LdmUser, ldm_user, G_TYPE_OBJECT);

#define GET_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LDM_TYPE_USER, LdmUserPrivate)

/**
 * ldm_user_new:
 * 
 * Create a new user.
 * @greeter: The greeter the user is connected to
 * @name: The username
 * @real_name: The real name of the user
 * @home_directory: The home directory of the user
 * @image: The image URI
 * @logged_in: TRUE if this user is currently logged in
 * 
 * Return value: the new #LdmUser
 **/
LdmUser *
ldm_user_new (LdmGreeter *greeter, const gchar *name, const gchar *real_name, const gchar *home_directory, const gchar *image, gboolean logged_in)
{
    return g_object_new (LDM_TYPE_USER, "greeter", greeter, "name", name, "real-name", real_name, "home-directory", home_directory, "image", image, "logged-in", logged_in, NULL);
}

gboolean
ldm_user_update (LdmUser *user, const gchar *real_name, const gchar *home_directory, const gchar *image, gboolean logged_in)
{
    LdmUserPrivate *priv = GET_PRIVATE (user);

    if (g_strcmp0 (priv->real_name, real_name) == 0 &&
        g_strcmp0 (priv->home_directory, home_directory) == 0 &&
        g_strcmp0 (priv->image, image) == 0 &&
        priv->logged_in == logged_in)
        return FALSE;
  
    g_free (priv->real_name);
    priv->real_name = g_strdup (real_name);
    g_free (priv->home_directory);
    priv->home_directory = g_strdup (home_directory);
    g_free (priv->image);
    priv->image = g_strdup (image);
    priv->logged_in = logged_in;

    return TRUE;
}

/**
 * ldm_user_get_name:
 * @user: A #LdmUser
 * 
 * Get the name of a user.
 * 
 * Return value: The name of the given user
 **/
const gchar *
ldm_user_get_name (LdmUser *user)
{
    g_return_val_if_fail (LDM_IS_USER (user), NULL);
    return GET_PRIVATE (user)->name;
}

void
ldm_user_set_name (LdmUser *user, const gchar *name)
{
    LdmUserPrivate *priv;

    g_return_if_fail (LDM_IS_USER (user));

    priv = GET_PRIVATE (user);
    g_free (priv->name);
    priv->name = g_strdup (name);
}

/**
 * ldm_user_get_real_name:
 * @user: A #LdmUser
 * 
 * Get the real name of a user.
 *
 * Return value: The real name of the given user (may be blank)
 **/
const gchar *
ldm_user_get_real_name (LdmUser *user)
{
    g_return_val_if_fail (LDM_IS_USER (user), NULL);
    return GET_PRIVATE (user)->real_name;
}

void
ldm_user_set_real_name (LdmUser *user, const gchar *real_name)
{
    LdmUserPrivate *priv;

    g_return_if_fail (LDM_IS_USER (user));

    priv = GET_PRIVATE (user);
    g_free (priv->real_name);
    priv->real_name = g_strdup (real_name);
}

/**
 * ldm_user_get_display_name:
 * @user: A #LdmUser
 * 
 * Get the display name of a user.
 * 
 * Return value: The display name of the given user
 **/
const gchar *
ldm_user_get_display_name (LdmUser *user)
{
    LdmUserPrivate *priv;

    g_return_val_if_fail (LDM_IS_USER (user), NULL);

    priv = GET_PRIVATE (user);
    if (priv->real_name)
        return priv->real_name;
    else
        return priv->name;
}

/**
 * ldm_user_get_home_directory:
 * @user: A #LdmUser
 * 
 * Get the home directory for a user.
 * 
 * Return value: The users home directory
 */
const gchar *
ldm_user_get_home_directory (LdmUser *user)
{
    g_return_val_if_fail (LDM_IS_USER (user), NULL);
    return GET_PRIVATE (user)->home_directory;
}

void
ldm_user_set_home_directory (LdmUser *user, const gchar *home_directory)
{
    LdmUserPrivate *priv;

    g_return_if_fail (LDM_IS_USER (user));

    priv = GET_PRIVATE (user);
    g_free (priv->home_directory);
    priv->home_directory = g_strdup (home_directory);
}

/**
 * ldm_user_get_image:
 * @user: A #LdmUser
 * 
 * Get the image URI for a user.
 * 
 * Return value: The image URI for the given user or NULL if no URI
 **/
const gchar *
ldm_user_get_image (LdmUser *user)
{
    g_return_val_if_fail (LDM_IS_USER (user), NULL);
    return GET_PRIVATE (user)->image;
}

void
ldm_user_set_image (LdmUser *user, const gchar *image)
{
    LdmUserPrivate *priv;

    g_return_if_fail (LDM_IS_USER (user));

    priv = GET_PRIVATE (user);
    g_free (priv->image);
    priv->image = g_strdup (image);
}

static void
load_dmrc (LdmUser *user)
{
    LdmUserPrivate *priv = GET_PRIVATE (user);
    gchar *path;
    gboolean have_dmrc;

    priv->dmrc_file = g_key_file_new ();

    /* Load from the user directory */  
    path = g_build_filename (priv->home_directory, ".dmrc", NULL);
    have_dmrc = g_key_file_load_from_file (priv->dmrc_file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_free (path);

    /* If no ~/.dmrc, then load from the cache */
    // FIXME

    // FIXME: Watch for changes

    priv->language = g_key_file_get_string (priv->dmrc_file, "Desktop", "Language", NULL);
    priv->layout = g_key_file_get_string (priv->dmrc_file, "Desktop", "Layout", NULL);
    priv->session = g_key_file_get_string (priv->dmrc_file, "Desktop", "Session", NULL);
}

/**
 * ldm_user_get_language
 * @user: A #LdmUser
 * 
 * Get the language for a user.
 * 
 * Return value: The language for the given user or NULL if using system defaults.
 **/
const gchar *
ldm_user_get_language (LdmUser *user)
{
    g_return_val_if_fail (LDM_IS_USER (user), NULL);
    load_dmrc (user);
    return GET_PRIVATE (user)->language;
}

/**
 * ldm_user_get_layout
 * @user: A #LdmUser
 * 
 * Get the keyboard layout for a user.
 * 
 * Return value: The keyboard layoyt for the given user or NULL if using system defaults.
 **/
const gchar *
ldm_user_get_layout (LdmUser *user)
{
    g_return_val_if_fail (LDM_IS_USER (user), NULL);
    load_dmrc (user);
    return GET_PRIVATE (user)->layout;
}

/**
 * ldm_user_get_session
 * @user: A #LdmUser
 * 
 * Get the session for a user.
 * 
 * Return value: The session for the given user or NULL if using system defaults.
 **/
const gchar *
ldm_user_get_session (LdmUser *user)
{
    g_return_val_if_fail (LDM_IS_USER (user), NULL);
    load_dmrc (user);
    return GET_PRIVATE (user)->session; 
}

/**
 * ldm_user_get_logged_in:
 * @user: A #LdmUser
 * 
 * Check if a user is logged in.
 * 
 * Return value: TRUE if the user is currently logged in.
 **/
gboolean
ldm_user_get_logged_in (LdmUser *user)
{
    g_return_val_if_fail (LDM_IS_USER (user), FALSE);
    return GET_PRIVATE (user)->logged_in;
}

void
ldm_user_set_logged_in (LdmUser *user, gboolean logged_in)
{
    g_return_if_fail (LDM_IS_USER (user));
    GET_PRIVATE (user)->logged_in = logged_in;
}

static void
ldm_user_init (LdmUser *user)
{
}

static void
ldm_user_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LdmUser *self = LDM_USER (object);
    LdmUserPrivate *priv = GET_PRIVATE (self);

    switch (prop_id) {
    case PROP_GREETER:
        priv->greeter = g_object_ref (g_value_get_object (value));
        break;
    case PROP_NAME:
        ldm_user_set_name (self, g_value_get_string (value));
        break;
    case PROP_REAL_NAME:
        ldm_user_set_real_name (self, g_value_get_string (value));
        break;
    case PROP_HOME_DIRECTORY:
        ldm_user_set_home_directory (self, g_value_get_string (value));
        break;
    case PROP_IMAGE:
        ldm_user_set_image (self, g_value_get_string (value));
        break;
    case PROP_LOGGED_IN:
        ldm_user_set_logged_in (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ldm_user_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LdmUser *self;

    self = LDM_USER (object);

    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string (value, ldm_user_get_name (self));
        break;
    case PROP_REAL_NAME:
        g_value_set_string (value, ldm_user_get_real_name (self));
        break;
    case PROP_DISPLAY_NAME:
        g_value_set_string (value, ldm_user_get_display_name (self));
        break;
    case PROP_HOME_DIRECTORY:
        g_value_set_string (value, ldm_user_get_home_directory (self));
        break;
    case PROP_IMAGE:
        g_value_set_string (value, ldm_user_get_image (self));
        break;
    case PROP_LANGUAGE:
        g_value_set_string (value, ldm_user_get_language (self));
        break;
    case PROP_LAYOUT:
        g_value_set_string (value, ldm_user_get_layout (self));
        break;
    case PROP_SESSION:
        g_value_set_string (value, ldm_user_get_session (self));
        break;
    case PROP_LOGGED_IN:
        g_value_set_boolean (value, ldm_user_get_logged_in (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ldm_user_finalize (GObject *object)
{
    LdmUser *self = LDM_USER (object);
    LdmUserPrivate *priv = GET_PRIVATE (self);

    g_free (priv->name);
    g_free (priv->real_name);
    g_free (priv->home_directory);
    g_free (priv->image);
    if (priv->dmrc_file)
        g_key_file_free (priv->dmrc_file);
}

static void
ldm_user_class_init (LdmUserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
    g_type_class_add_private (klass, sizeof (LdmUserPrivate));

    object_class->set_property = ldm_user_set_property;
    object_class->get_property = ldm_user_get_property;
    object_class->finalize = ldm_user_finalize;

    g_object_class_install_property(object_class,
                                    PROP_GREETER,
                                    g_param_spec_object("greeter",
                                                        "greeter",
                                                        "Greeter",
                                                        LDM_TYPE_GREETER,
                                                        G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_NAME,
                                    g_param_spec_string("name",
                                                        "name",
                                                        "Username",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_REAL_NAME,
                                    g_param_spec_string("real-name",
                                                        "real-name",
                                                        "Users real name",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_DISPLAY_NAME,
                                    g_param_spec_string("display-name",
                                                        "display-name",
                                                        "Users display name",
                                                        NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_HOME_DIRECTORY,
                                    g_param_spec_string("home-directory",
                                                        "home-directory",
                                                        "Home directory",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_IMAGE,
                                    g_param_spec_string("image",
                                                        "image",
                                                        "Avatar image",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_LANGUAGE,
                                    g_param_spec_string("language",
                                                        "language",
                                                        "Language used by this user",
                                                        NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_LAYOUT,
                                    g_param_spec_string("layout",
                                                        "layout",
                                                        "Keyboard layout used by this user",
                                                        NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_SESSION,
                                    g_param_spec_string("session",
                                                        "session",
                                                        "Session used by this user",
                                                        NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_LOGGED_IN,
                                    g_param_spec_boolean("logged-in",
                                                         "logged-in",
                                                         "TRUE if the user is currently in a session",
                                                         FALSE,
                                                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
