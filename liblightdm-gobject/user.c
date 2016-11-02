/* -*- Mode: C; indent-tabs-mode:nil; tab-width:4 -*-
 *
 * Copyright (C) 2010 Robert Ancell.
 * Copyright (C) 2014 Canonical, Ltd.
 * Authors: Robert Ancell <robert.ancell@canonical.com>
 *          Michael Terry <michael.terry@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include <config.h>

#include "user-list.h"
#include "lightdm/user.h"

/**
 * SECTION:user-list
 * @short_description: Get information on user accounts on this system
 * @include: lightdm.h
 *
 * An object that contains information about local user accounts.
 */

/**
 * SECTION:user
 * @short_description: Get information on a user account
 * @include: lightdm.h
 *
 * Information about a local user account.
 */

/**
 * LightDMUserList:
 *
 * #LightDMUserList is an opaque data structure and can only be accessed
 * using the provided functions.
 */

/**
 * LightDMUserListClass:
 *
 * Class structure for #LightDMUserList.
 */

/**
 * LightDMUser:
 *
 * #LightDMUser is an opaque data structure and can only be accessed
 * using the provided functions.
 */

/**
 * LightDMUserClass:
 *
 * Class structure for #LightDMUser.
 */

enum
{
    LIST_PROP_NUM_USERS = 1,
    LIST_PROP_LENGTH,  
    LIST_PROP_USERS,
};

enum
{
    USER_PROP_COMMON_USER = 1,
    USER_PROP_NAME,
    USER_PROP_REAL_NAME,
    USER_PROP_DISPLAY_NAME,
    USER_PROP_HOME_DIRECTORY,
    USER_PROP_IMAGE,
    USER_PROP_BACKGROUND,
    USER_PROP_LANGUAGE,
    USER_PROP_LAYOUT,
    USER_PROP_LAYOUTS,
    USER_PROP_SESSION,
    USER_PROP_LOGGED_IN,
    USER_PROP_HAS_MESSAGES,
    USER_PROP_UID,
};

enum
{
    USER_ADDED,
    USER_CHANGED,
    USER_REMOVED,
    LAST_LIST_SIGNAL
};
static guint list_signals[LAST_LIST_SIGNAL] = { 0 };

enum
{
    CHANGED,
    LAST_USER_SIGNAL
};
static guint user_signals[LAST_USER_SIGNAL] = { 0 };

typedef struct
{
    gboolean initialized;

    /* Wrapper list, kept locally to preserve transfer-none promises */
    GList *lightdm_list;
} LightDMUserListPrivate;

typedef struct
{
    CommonUser *common_user;
} LightDMUserPrivate;

G_DEFINE_TYPE (LightDMUserList, lightdm_user_list, G_TYPE_OBJECT);
G_DEFINE_TYPE (LightDMUser, lightdm_user, G_TYPE_OBJECT);

#define GET_LIST_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_USER_LIST, LightDMUserListPrivate)
#define GET_USER_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_USER, LightDMUserPrivate)

static LightDMUserList *singleton = NULL;

/**
 * lightdm_user_list_get_instance:
 *
 * Get the user list.
 *
 * Return value: (transfer none): the #LightDMUserList
 **/
LightDMUserList *
lightdm_user_list_get_instance (void)
{
    if (!singleton)
        singleton = g_object_new (LIGHTDM_TYPE_USER_LIST, NULL);
    return singleton;
}

static void
user_changed_cb (CommonUser *common_user, LightDMUser *lightdm_user)
{
    g_signal_emit (lightdm_user, user_signals[CHANGED], 0);
}

static LightDMUser *
wrap_common_user (CommonUser *user)
{
    LightDMUser *lightdm_user = g_object_new (LIGHTDM_TYPE_USER, "common-user", user, NULL);
    g_signal_connect (user, USER_SIGNAL_CHANGED, G_CALLBACK (user_changed_cb), lightdm_user);
    return lightdm_user;
}

static void
user_list_added_cb (CommonUserList *common_list, CommonUser *common_user, LightDMUserList *user_list)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    GList *common_users = common_user_list_get_users (common_list);
    LightDMUser *lightdm_user = wrap_common_user (common_user);
    priv->lightdm_list = g_list_insert (priv->lightdm_list, lightdm_user, g_list_index (common_users, common_user));
    g_signal_emit (user_list, list_signals[USER_ADDED], 0, lightdm_user);
}

static void
user_list_changed_cb (CommonUserList *common_list, CommonUser *common_user, LightDMUserList *user_list)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    GList *common_users = common_user_list_get_users (common_list);
    LightDMUser *lightdm_user = g_list_nth_data (priv->lightdm_list, g_list_index (common_users, common_user));
    g_signal_emit (user_list, list_signals[USER_CHANGED], 0, lightdm_user);
}

static void
user_list_removed_cb (CommonUserList *common_list, CommonUser *common_user, LightDMUserList *user_list)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    GList *link;

    for (link = priv->lightdm_list; link; link = link->next)
    {
        LightDMUser *lightdm_user = link->data;
        LightDMUserPrivate *user_priv = GET_USER_PRIVATE (lightdm_user);
        if (user_priv->common_user == common_user)
        {
            priv->lightdm_list = g_list_delete_link (priv->lightdm_list, link);
            g_signal_emit (user_list, list_signals[USER_REMOVED], 0, lightdm_user);
            g_object_unref (lightdm_user);
            break;
        }
    }
}

static void
initialize_user_list_if_needed (LightDMUserList *user_list)
{
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (user_list);
    GList *common_users;
    GList *link;

    if (priv->initialized)
        return;

    common_users = common_user_list_get_users (common_user_list_get_instance ());
    for (link = common_users; link; link = link->next)
    {
        CommonUser *user = link->data;
        LightDMUser *lightdm_user = wrap_common_user (user);
        priv->lightdm_list = g_list_prepend (priv->lightdm_list, lightdm_user);
    }
    priv->lightdm_list = g_list_reverse (priv->lightdm_list);

    CommonUserList *common_list = common_user_list_get_instance ();
    g_signal_connect (common_list, USER_LIST_SIGNAL_USER_ADDED, G_CALLBACK (user_list_added_cb), user_list);
    g_signal_connect (common_list, USER_LIST_SIGNAL_USER_CHANGED, G_CALLBACK (user_list_changed_cb), user_list);
    g_signal_connect (common_list, USER_LIST_SIGNAL_USER_REMOVED, G_CALLBACK (user_list_removed_cb), user_list);

    priv->initialized = TRUE;
}

/**
 * lightdm_user_list_get_length:
 * @user_list: a #LightDMUserList
 *
 * Return value: The number of users able to log in
 **/
gint
lightdm_user_list_get_length (LightDMUserList *user_list)
{
    g_return_val_if_fail (LIGHTDM_IS_USER_LIST (user_list), 0);
    initialize_user_list_if_needed (user_list);
    return g_list_length (GET_LIST_PRIVATE (user_list)->lightdm_list);
}

/**
 * lightdm_user_list_get_users:
 * @user_list: A #LightDMUserList
 *
 * Get a list of users to present to the user.  This list may be a subset of the
 * available users and may be empty depending on the server configuration.
 *
 * Return value: (element-type LightDMUser) (transfer none): A list of #LightDMUser that should be presented to the user.
 **/
GList *
lightdm_user_list_get_users (LightDMUserList *user_list)
{
    g_return_val_if_fail (LIGHTDM_IS_USER_LIST (user_list), NULL);
    initialize_user_list_if_needed (user_list);
    return GET_LIST_PRIVATE (user_list)->lightdm_list;
}

/**
 * lightdm_user_list_get_user_by_name:
 * @user_list: A #LightDMUserList
 * @username: Name of user to get.
 *
 * Get infomation about a given user or #NULL if this user doesn't exist.
 *
 * Return value: (transfer none): A #LightDMUser entry for the given user.
 **/
LightDMUser *
lightdm_user_list_get_user_by_name (LightDMUserList *user_list, const gchar *username)
{
    GList *link;

    g_return_val_if_fail (LIGHTDM_IS_USER_LIST (user_list), NULL);
    g_return_val_if_fail (username != NULL, NULL);

    initialize_user_list_if_needed (user_list);

    for (link = GET_LIST_PRIVATE (user_list)->lightdm_list; link; link = link->next)
    {
        LightDMUser *user = link->data;
        if (g_strcmp0 (lightdm_user_get_name (user), username) == 0)
            return user;
    }

    return NULL;
}

static void
lightdm_user_list_init (LightDMUserList *user_list)
{
}

static void
lightdm_user_list_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lightdm_user_list_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LightDMUserList *self;

    self = LIGHTDM_USER_LIST (object);

    switch (prop_id)
    {
    case LIST_PROP_NUM_USERS:
    case LIST_PROP_LENGTH:      
        g_value_set_int (value, lightdm_user_list_get_length (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_user_list_finalize (GObject *object)
{
    LightDMUserList *self = LIGHTDM_USER_LIST (object);
    LightDMUserListPrivate *priv = GET_LIST_PRIVATE (self);

    g_list_free_full (priv->lightdm_list, g_object_unref);

    G_OBJECT_CLASS (lightdm_user_list_parent_class)->finalize (object);
}

static void
lightdm_user_list_class_init (LightDMUserListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (LightDMUserListPrivate));

    object_class->set_property = lightdm_user_list_set_property;
    object_class->get_property = lightdm_user_list_get_property;
    object_class->finalize = lightdm_user_list_finalize;

    g_object_class_install_property (object_class,
                                     LIST_PROP_NUM_USERS,
                                     g_param_spec_int ("num-users",
                                                       "num-users",
                                                       "Number of login users",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_DEPRECATED | G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     LIST_PROP_LENGTH,
                                     g_param_spec_int ("length",
                                                       "length",
                                                       "Number of login users",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READABLE));

    /*g_object_class_install_property (object_class,
                                     LIST_PROP_USERS,
                                     g_param_spec_int ("users",
                                                       "users",
                                                       "Users to present to user",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READABLE));*/
    /**
     * LightDMUserList::user-added:
     * @user_list: A #LightDMUserList
     * @user: The #LightDMUser that has been added.
     *
     * The ::user-added signal gets emitted when a user account is created.
     **/
    list_signals[USER_ADDED] =
        g_signal_new (LIGHTDM_USER_LIST_SIGNAL_USER_ADDED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserListClass, user_added),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);

    /**
     * LightDMUserList::user-changed:
     * @user_list: A #LightDMUserList
     * @user: The #LightDMUser that has been changed.
     *
     * The ::user-changed signal gets emitted when a user account is modified.
     **/
    list_signals[USER_CHANGED] =
        g_signal_new (LIGHTDM_USER_LIST_SIGNAL_USER_CHANGED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserListClass, user_changed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);

    /**
     * LightDMUserList::user-removed:
     * @user_list: A #LightDMUserList
     * @user: The #LightDMUser that has been removed.
     *
     * The ::user-removed signal gets emitted when a user account is removed.
     **/
    list_signals[USER_REMOVED] =
        g_signal_new (LIGHTDM_USER_LIST_SIGNAL_USER_REMOVED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserListClass, user_removed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);
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
    return common_user_get_name (GET_USER_PRIVATE (user)->common_user);
}

/**
 * lightdm_user_get_real_name:
 * @user: A #LightDMUser
 *
 * Get the real name of a user.
 *
 * Return value: The real name of the given user
 **/
const gchar *
lightdm_user_get_real_name (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return common_user_get_real_name (GET_USER_PRIVATE (user)->common_user);
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
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return common_user_get_display_name (GET_USER_PRIVATE (user)->common_user);
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
    return common_user_get_home_directory (GET_USER_PRIVATE (user)->common_user);
}

/**
 * lightdm_user_get_image:
 * @user: A #LightDMUser
 *
 * Get the image URI for a user.
 *
 * Return value: (nullable): The image URI for the given user or #NULL if no URI
 **/
const gchar *
lightdm_user_get_image (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return common_user_get_image (GET_USER_PRIVATE (user)->common_user);
}

/**
 * lightdm_user_get_background:
 * @user: A #LightDMUser
 *
 * Get the background file path for a user.
 *
 * Return value: (nullable): The background file path for the given user or #NULL if no path
 **/
const gchar *
lightdm_user_get_background (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return common_user_get_background (GET_USER_PRIVATE (user)->common_user);
}

/**
 * lightdm_user_get_language:
 * @user: A #LightDMUser
 *
 * Get the language for a user.
 *
 * Return value: (nullable): The language in the form of a local specification (e.g. "de_DE.UTF-8") for the given user or #NULL if using the system default locale.
 **/
const gchar *
lightdm_user_get_language (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return common_user_get_language (GET_USER_PRIVATE (user)->common_user);
}

/**
 * lightdm_user_get_layout:
 * @user: A #LightDMUser
 *
 * Get the keyboard layout for a user.
 *
 * Return value: (nullable): The keyboard layout for the given user or #NULL if using system defaults.  Copy the value if you want to use it long term.
 **/
const gchar *
lightdm_user_get_layout (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return common_user_get_layout (GET_USER_PRIVATE (user)->common_user);
}

/**
 * lightdm_user_get_layouts:
 * @user: A #LightDMUser
 *
 * Get the configured keyboard layouts for a user.
 *
 * Return value: (transfer none) (array zero-terminated=1): A NULL-terminated array of keyboard layouts for the given user.  Copy the values if you want to use them long term.
 **/
const gchar * const *
lightdm_user_get_layouts (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return common_user_get_layouts (GET_USER_PRIVATE (user)->common_user);
}

/**
 * lightdm_user_get_session:
 * @user: A #LightDMUser
 *
 * Get the session for a user.
 *
 * Return value: (nullable): The session for the given user or #NULL if using system defaults.
 **/
const gchar *
lightdm_user_get_session (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), NULL);
    return common_user_get_session (GET_USER_PRIVATE (user)->common_user);
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
    return common_user_get_logged_in (GET_USER_PRIVATE (user)->common_user);
}

/**
 * lightdm_user_get_has_messages:
 * @user: A #LightDMUser
 *
 * Check if a user has waiting messages.
 *
 * Return value: #TRUE if the user has waiting messages.
 **/
gboolean
lightdm_user_get_has_messages (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), FALSE);
    return common_user_get_has_messages (GET_USER_PRIVATE (user)->common_user);
}

/**
 * lightdm_user_get_uid:
 * @user: A #LightDMUser
 *
 * Get the uid of a user.
 *
 * Return value: The uid of the given user
 **/
uid_t
lightdm_user_get_uid (LightDMUser *user)
{
    g_return_val_if_fail (LIGHTDM_IS_USER (user), (uid_t)-1);
    return common_user_get_uid (GET_USER_PRIVATE (user)->common_user);
}

static void
lightdm_user_init (LightDMUser *user)
{
}

static void
lightdm_user_set_property (GObject    *object,
                           guint       prop_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
    LightDMUser *self = LIGHTDM_USER (object);
    LightDMUserPrivate *priv = GET_USER_PRIVATE (self);

    switch (prop_id)
    {
    case USER_PROP_COMMON_USER:
        priv->common_user = g_value_dup_object (value);
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

    switch (prop_id)
    {
    case USER_PROP_NAME:
        g_value_set_string (value, lightdm_user_get_name (self));
        break;
    case USER_PROP_REAL_NAME:
        g_value_set_string (value, lightdm_user_get_real_name (self));
        break;
    case USER_PROP_DISPLAY_NAME:
        g_value_set_string (value, lightdm_user_get_display_name (self));
        break;
    case USER_PROP_HOME_DIRECTORY:
        g_value_set_string (value, lightdm_user_get_home_directory (self));
        break;
    case USER_PROP_IMAGE:
        g_value_set_string (value, lightdm_user_get_image (self));
        break;
    case USER_PROP_BACKGROUND:
        g_value_set_string (value, lightdm_user_get_background (self));
        break;
    case USER_PROP_LANGUAGE:
        g_value_set_string (value, lightdm_user_get_language (self));
        break;
    case USER_PROP_LAYOUT:
        g_value_set_string (value, lightdm_user_get_layout (self));
        break;
    case USER_PROP_LAYOUTS:
        g_value_set_boxed (value, g_strdupv ((gchar **) lightdm_user_get_layouts (self)));
        break;
    case USER_PROP_SESSION:
        g_value_set_string (value, lightdm_user_get_session (self));
        break;
    case USER_PROP_LOGGED_IN:
        g_value_set_boolean (value, lightdm_user_get_logged_in (self));
        break;
    case USER_PROP_HAS_MESSAGES:
        g_value_set_boolean (value, lightdm_user_get_has_messages (self));
        break;
    case USER_PROP_UID:
        g_value_set_uint64 (value, lightdm_user_get_uid (self));
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
    LightDMUserPrivate *priv = GET_USER_PRIVATE (self);

    g_object_unref (priv->common_user);

    G_OBJECT_CLASS (lightdm_user_parent_class)->finalize (object);
}

static void
lightdm_user_class_init (LightDMUserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (LightDMUserPrivate));

    object_class->set_property = lightdm_user_set_property;
    object_class->get_property = lightdm_user_get_property;
    object_class->finalize = lightdm_user_finalize;

    g_object_class_install_property (object_class,
                                     USER_PROP_COMMON_USER,
                                     g_param_spec_object ("common-user",
                                                          "common-user",
                                                          "Internal user object",
                                                          COMMON_TYPE_USER,
                                                          G_PARAM_PRIVATE|G_PARAM_CONSTRUCT_ONLY|G_PARAM_WRITABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_NAME,
                                     g_param_spec_string ("name",
                                                          "name",
                                                          "Username",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_REAL_NAME,
                                     g_param_spec_string ("real-name",
                                                          "real-name",
                                                          "Users real name",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_DISPLAY_NAME,
                                     g_param_spec_string ("display-name",
                                                          "display-name",
                                                          "Users display name",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_HOME_DIRECTORY,
                                     g_param_spec_string ("home-directory",
                                                          "home-directory",
                                                          "Home directory",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_IMAGE,
                                     g_param_spec_string ("image",
                                                          "image",
                                                          "Avatar image",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_BACKGROUND,
                                     g_param_spec_string ("background",
                                                          "background",
                                                          "User background",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LANGUAGE,
                                     g_param_spec_string ("language",
                                                         "language",
                                                         "Language used by this user",
                                                         NULL,
                                                         G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LAYOUT,
                                     g_param_spec_string ("layout",
                                                          "layout",
                                                          "Keyboard layout used by this user",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LAYOUTS,
                                     g_param_spec_boxed ("layouts",
                                                         "layouts",
                                                         "Keyboard layouts used by this user",
                                                         G_TYPE_STRV,
                                                         G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_SESSION,
                                     g_param_spec_string ("session",
                                                          "session",
                                                          "Session used by this user",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LOGGED_IN,
                                     g_param_spec_boolean ("logged-in",
                                                           "logged-in",
                                                           "TRUE if the user is currently in a session",
                                                           FALSE,
                                                           G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LOGGED_IN,
                                     g_param_spec_boolean ("has-messages",
                                                           "has-messages",
                                                           "TRUE if the user is has waiting messages",
                                                           FALSE,
                                                           G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_UID,
                                     g_param_spec_uint64 ("uid",
                                                          "uid",
                                                          "User UID",
                                                          0, G_MAXUINT64, 0,
                                                          G_PARAM_READABLE));

    /**
     * LightDMUser::changed:
     * @user: A #LightDMUser
     *
     * The ::changed signal gets emitted this user account is modified.
     **/
    user_signals[CHANGED] =
        g_signal_new (LIGHTDM_SIGNAL_USER_CHANGED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserClass, changed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}
