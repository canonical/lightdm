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
    LightDMGreeter *greeter;

    gchar *name;
    gchar *real_name;
    gchar *home_directory;
    gchar *image;
    gboolean logged_in;

    GKeyFile *dmrc_file;
    gchar *language;
    gchar *layout;
    gchar *session;
} LightDMUserPrivate;

G_DEFINE_TYPE (LightDMUser, lightdm_user, G_TYPE_OBJECT);

#define GET_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_USER, LightDMUserPrivate)

/**
 * lightdm_user_new:
 * 
 * Create a new user.
 * @greeter: The greeter the user is connected to
 * @name: The username
 * @real_name: The real name of the user
 * @home_directory: The home directory of the user
 * @image: The image URI
 * @logged_in: #TRUE if this user is currently logged in
 * 
 * Return value: the new #LightDMUser
 **/
LightDMUser *
lightdm_user_new (LightDMGreeter *greeter, const gchar *name, const gchar *real_name, const gchar *home_directory, const gchar *image, gboolean logged_in)
{
    return g_object_new (LIGHTDM_TYPE_USER, "greeter", greeter, "name", name, "real-name", real_name, "home-directory", home_directory, "image", image, "logged-in", logged_in, NULL);
}

gboolean
lightdm_user_update (LightDMUser *user, const gchar *real_name, const gchar *home_directory, const gchar *image, gboolean logged_in)
{
    LightDMUserPrivate *priv = GET_PRIVATE (user);

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
 * lightdm_user_get_name:
 * @user: A #LightDMUser
 * 
 * Get the name of a user.
 * 
 * Return value: The name of the given user
 **/
const gchar *
lightdm_user_get_name (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return GET_PRIVATE (user)->name;
}

void
lightdm_user_set_name (LightDMUser *user, const gchar *name)
{
    LightDMUserPrivate *priv;

    g_return_if_fail (LIGHTDM_IS_USER (user));

    priv = GET_PRIVATE (user);
    g_free (priv->name);
    priv->name = g_strdup (name);
}

/**
 * lightdm_user_get_real_name:
 * @user: A #LightDMUser
 * 
 * Get the real name of a user.
 *
 * Return value: The real name of the given user (may be blank)
 **/
const gchar *
lightdm_user_get_real_name (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return GET_PRIVATE (user)->real_name;
}

void
lightdm_user_set_real_name (LightDMUser *user, const gchar *real_name)
{
    LightDMUserPrivate *priv;

    g_return_if_fail (LIGHTDM_IS_USER (user));

    priv = GET_PRIVATE (user);
    g_free (priv->real_name);
    priv->real_name = g_strdup (real_name);
}

/**
 * lightdm_user_get_display_name:
 * @user: A #LightDMUser
 * 
 * Get the display name of a user.
 * 
 * Return value: The display name of the given user
 **/
const gchar *
lightdm_user_get_display_name (LightDMUser *user)
{
    LightDMUserPrivate *priv;

    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);

    priv = GET_PRIVATE (user);
    if (priv->real_name)
        return priv->real_name;
    else
        return priv->name;
}

/**
 * lightdm_user_get_home_directory:
 * @user: A #LightDMUser
 * 
 * Get the home directory for a user.
 * 
 * Return value: The users home directory
 */
const gchar *
lightdm_user_get_home_directory (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return GET_PRIVATE (user)->home_directory;
}

void
lightdm_user_set_home_directory (LightDMUser *user, const gchar *home_directory)
{
    LightDMUserPrivate *priv;

    g_return_if_fail (LIGHTDM_IS_USER (user));

    priv = GET_PRIVATE (user);
    g_free (priv->home_directory);
    priv->home_directory = g_strdup (home_directory);
}

/**
 * lightdm_user_get_image:
 * @user: A #LightDMUser
 * 
 * Get the image URI for a user.
 * 
 * Return value: The image URI for the given user or #NULL if no URI
 **/
const gchar *
lightdm_user_get_image (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return GET_PRIVATE (user)->image;
}

void
lightdm_user_set_image (LightDMUser *user, const gchar *image)
{
    LightDMUserPrivate *priv;

    g_return_if_fail (LIGHTDM_IS_USER (user));

    priv = GET_PRIVATE (user);
    g_free (priv->image);
    priv->image = g_strdup (image);
}

static void
load_dmrc (LightDMUser *user)
{
    LightDMUserPrivate *priv = GET_PRIVATE (user);
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
 * lightdm_user_get_language
 * @user: A #LightDMUser
 * 
 * Get the language for a user.
 * 
 * Return value: The language for the given user or #NULL if using system defaults.
 **/
const gchar *
lightdm_user_get_language (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_dmrc (user);
    return GET_PRIVATE (user)->language;
}

/**
 * lightdm_user_get_layout
 * @user: A #LightDMUser
 * 
 * Get the keyboard layout for a user.
 * 
 * Return value: The keyboard layoyt for the given user or #NULL if using system defaults.
 **/
const gchar *
lightdm_user_get_layout (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_dmrc (user);
    return GET_PRIVATE (user)->layout;
}

/**
 * lightdm_user_get_session
 * @user: A #LightDMUser
 * 
 * Get the session for a user.
 * 
 * Return value: The session for the given user or #NULL if using system defaults.
 **/
const gchar *
lightdm_user_get_session (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    load_dmrc (user);
    return GET_PRIVATE (user)->session; 
}

/**
 * lightdm_user_get_logged_in:
 * @user: A #LightDMUser
 * 
 * Check if a user is logged in.
 * 
 * Return value: #TRUE if the user is currently logged in.
 **/
gboolean
lightdm_user_get_logged_in (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), FALSE);
    return GET_PRIVATE (user)->logged_in;
}

void
lightdm_user_set_logged_in (LightDMUser *user, gboolean logged_in)
{
    g_return_if_fail (LIGHTDM_IS_USER (user));
    GET_PRIVATE (user)->logged_in = logged_in;
}

static void
lightdm_user_init (LightDMUser *user)
{
}

static void
lightdm_user_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LightDMUser *self = LIGHTDM_USER (object);
    LightDMUserPrivate *priv = GET_PRIVATE (self);

    switch (prop_id) {
    case PROP_GREETER:
        priv->greeter = g_object_ref (g_value_get_object (value));
        break;
    case PROP_NAME:
        lightdm_user_set_name (self, g_value_get_string (value));
        break;
    case PROP_REAL_NAME:
        lightdm_user_set_real_name (self, g_value_get_string (value));
        break;
    case PROP_HOME_DIRECTORY:
        lightdm_user_set_home_directory (self, g_value_get_string (value));
        break;
    case PROP_IMAGE:
        lightdm_user_set_image (self, g_value_get_string (value));
        break;
    case PROP_LOGGED_IN:
        lightdm_user_set_logged_in (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_user_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LightDMUser *self;

    self = LIGHTDM_USER (object);

    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string (value, lightdm_user_get_name (self));
        break;
    case PROP_REAL_NAME:
        g_value_set_string (value, lightdm_user_get_real_name (self));
        break;
    case PROP_DISPLAY_NAME:
        g_value_set_string (value, lightdm_user_get_display_name (self));
        break;
    case PROP_HOME_DIRECTORY:
        g_value_set_string (value, lightdm_user_get_home_directory (self));
        break;
    case PROP_IMAGE:
        g_value_set_string (value, lightdm_user_get_image (self));
        break;
    case PROP_LANGUAGE:
        g_value_set_string (value, lightdm_user_get_language (self));
        break;
    case PROP_LAYOUT:
        g_value_set_string (value, lightdm_user_get_layout (self));
        break;
    case PROP_SESSION:
        g_value_set_string (value, lightdm_user_get_session (self));
        break;
    case PROP_LOGGED_IN:
        g_value_set_boolean (value, lightdm_user_get_logged_in (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_user_finalize (GObject *object)
{
    LightDMUser *self = LIGHTDM_USER (object);
    LightDMUserPrivate *priv = GET_PRIVATE (self);

    g_free (priv->name);
    g_free (priv->real_name);
    g_free (priv->home_directory);
    g_free (priv->image);
    if (priv->dmrc_file)
        g_key_file_free (priv->dmrc_file);
}

static void
lightdm_user_class_init (LightDMUserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
    g_type_class_add_private (klass, sizeof (LightDMUserPrivate));

    object_class->set_property = lightdm_user_set_property;
    object_class->get_property = lightdm_user_get_property;
    object_class->finalize = lightdm_user_finalize;

    g_object_class_install_property(object_class,
                                    PROP_GREETER,
                                    g_param_spec_object("greeter",
                                                        "greeter",
                                                        "Greeter",
                                                        LIGHTDM_TYPE_GREETER,
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
