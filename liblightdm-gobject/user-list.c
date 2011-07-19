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

#include <config.h>

#include <errno.h>
#include <string.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <gio/gio.h>

#include "lightdm/user-list.h"

enum {
    PROP_0,
    PROP_NUM_USERS,
    PROP_USERS,
};

enum {
    USER_ADDED,
    USER_CHANGED,
    USER_REMOVED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    /* Connection to system bus */
    GDBusConnection *system_bus;

    /* File monitor for password file */
    GFileMonitor *passwd_monitor;
  
    /* TRUE if have scanned users */
    gboolean have_users;

    /* List of users */
    GList *users;
} LightDMUserListPrivate;

G_DEFINE_TYPE (LightDMUserList, lightdm_user_list, G_TYPE_OBJECT);

#define GET_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_USER_LIST, LightDMUserListPrivate)

#define PASSWD_FILE      "/etc/passwd"
#define USER_CONFIG_FILE "/etc/lightdm/users.conf"

/**
 * lightdm_user_list_new:
 *
 * Create a new user list.
 *
 * Return value: the new #LightDMUserList
 **/
LightDMUserList *
lightdm_user_list_new ()
{
    return g_object_new (LIGHTDM_TYPE_USER_LIST, NULL);
}

static LightDMUser *
get_user_by_name (LightDMUserList *user_list, const gchar *username)
{
    LightDMUserListPrivate *priv = GET_PRIVATE (user_list);
    GList *link;
  
    for (link = priv->users; link; link = link->next)
    {
        LightDMUser *user = link->data;
        if (strcmp (lightdm_user_get_name (user), username) == 0)
            return user;
    }

    return NULL;
}
  
static gint
compare_user (gconstpointer a, gconstpointer b)
{
    LightDMUser *user_a = (LightDMUser *) a, *user_b = (LightDMUser *) b;
    return strcmp (lightdm_user_get_display_name (user_a), lightdm_user_get_display_name (user_b));
}

static gboolean
update_user (LightDMUser *user, const gchar *real_name, const gchar *home_directory, const gchar *image, gboolean logged_in)
{
    if (g_strcmp0 (lightdm_user_get_real_name (user), real_name) == 0 &&
        g_strcmp0 (lightdm_user_get_home_directory (user), home_directory) == 0 &&
        g_strcmp0 (lightdm_user_get_image (user), image) == 0 &&
        lightdm_user_get_logged_in (user) == logged_in)
        return FALSE;

    g_object_set (user, "real-name", real_name, "home-directory", home_directory, "image", image, "logged-in", logged_in, NULL);
    // FIXME: emit changed

    return TRUE;
}

static void
load_users (LightDMUserList *user_list)
{
    LightDMUserListPrivate *priv = GET_PRIVATE (user_list);
    GKeyFile *config;
    gchar *value;
    gint minimum_uid;
    gchar **hidden_users, **hidden_shells;
    GList *users = NULL, *old_users, *new_users = NULL, *changed_users = NULL, *link;
    GError *error = NULL;

    g_debug ("Loading user config from %s", USER_CONFIG_FILE);

    config = g_key_file_new ();
    if (!g_key_file_load_from_file (config, USER_CONFIG_FILE, G_KEY_FILE_NONE, &error) &&
        !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Failed to load configuration from %s: %s", USER_CONFIG_FILE, error->message); // FIXME: Don't make warning on no file, just info
    g_clear_error (&error);

    if (g_key_file_has_key (config, "UserList", "minimum-uid", NULL))
        minimum_uid = g_key_file_get_integer (config, "UserList", "minimum-uid", NULL);
    else
        minimum_uid = 500;

    value = g_key_file_get_string (config, "UserList", "hidden-users", NULL);
    if (!value)
        value = g_strdup ("nobody nobody4 noaccess");
    hidden_users = g_strsplit (value, " ", -1);
    g_free (value);

    value = g_key_file_get_string (config, "UserList", "hidden-shells", NULL);
    if (!value)
        value = g_strdup ("/bin/false /usr/sbin/nologin");
    hidden_shells = g_strsplit (value, " ", -1);
    g_free (value);

    g_key_file_free (config);

    setpwent ();

    while (TRUE)
    {
        struct passwd *entry;
        LightDMUser *user;
        char **tokens;
        gchar *real_name, *image;
        int i;

        errno = 0;
        entry = getpwent ();
        if (!entry)
            break;

        /* Ignore system users */
        if (entry->pw_uid < minimum_uid)
            continue;

        /* Ignore users disabled by shell */
        if (entry->pw_shell)
        {
            for (i = 0; hidden_shells[i] && strcmp (entry->pw_shell, hidden_shells[i]) != 0; i++);
            if (hidden_shells[i])
                continue;
        }

        /* Ignore certain users */
        for (i = 0; hidden_users[i] && strcmp (entry->pw_name, hidden_users[i]) != 0; i++);
        if (hidden_users[i])
            continue;

        tokens = g_strsplit (entry->pw_gecos, ",", -1);
        if (tokens[0] != NULL && tokens[0][0] != '\0')
            real_name = g_strdup (tokens[0]);
        else
            real_name = NULL;
        g_strfreev (tokens);
      
        image = g_build_filename (entry->pw_dir, ".face", NULL);
        if (!g_file_test (image, G_FILE_TEST_EXISTS))
        {
            g_free (image);
            image = g_build_filename (entry->pw_dir, ".face.icon", NULL);
            if (!g_file_test (image, G_FILE_TEST_EXISTS))
            {
                g_free (image);
                image = NULL;
            }
        }

        user = g_object_new (LIGHTDM_TYPE_USER, "name", entry->pw_name, "real-name", real_name, "home-directory", entry->pw_dir, "image", image, "logged-in", FALSE, NULL);
        g_free (real_name);
        g_free (image);

        /* Update existing users if have them */
        for (link = priv->users; link; link = link->next)
        {
            LightDMUser *info = link->data;
            if (strcmp (lightdm_user_get_name (info), lightdm_user_get_name (user)) == 0)
            {
                if (update_user (info, lightdm_user_get_real_name (user), lightdm_user_get_home_directory (user), lightdm_user_get_image (user), lightdm_user_get_logged_in (user)))
                    changed_users = g_list_insert_sorted (changed_users, info, compare_user);
                g_object_unref (user);
                user = info;
                break;
            }
        }
        if (!link)
        {
            /* Only notify once we have loaded the user list */
            if (priv->have_users)
                new_users = g_list_insert_sorted (new_users, user, compare_user);
        }
        users = g_list_insert_sorted (users, user, compare_user);
    }
    g_strfreev (hidden_users);
    g_strfreev (hidden_shells);

    if (errno != 0)
        g_warning ("Failed to read password database: %s", strerror (errno));

    endpwent ();

    /* Use new user list */
    old_users = priv->users;
    priv->users = users;
  
    /* Notify of changes */
    for (link = new_users; link; link = link->next)
    {
        LightDMUser *info = link->data;
        g_debug ("User %s added", lightdm_user_get_name (info));
        g_signal_emit (user_list, signals[USER_ADDED], 0, info);
    }
    g_list_free (new_users);
    for (link = changed_users; link; link = link->next)
    {
        LightDMUser *info = link->data;
        g_debug ("User %s changed", lightdm_user_get_name (info));
        g_signal_emit (user_list, signals[USER_CHANGED], 0, info);
    }
    g_list_free (changed_users);
    for (link = old_users; link; link = link->next)
    {
        GList *new_link;

        /* See if this user is in the current list */
        for (new_link = priv->users; new_link; new_link = new_link->next)
        {
            if (new_link->data == link->data)
                break;
        }

        if (!new_link)
        {
            LightDMUser *info = link->data;
            g_debug ("User %s removed", lightdm_user_get_name (info));
            g_signal_emit (user_list, signals[USER_REMOVED], 0, info);
            g_object_unref (info);
        }
    }
    g_list_free (old_users);
}

static void
passwd_changed_cb (GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, LightDMUserList *user_list)
{
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
        g_debug ("%s changed, reloading user list", g_file_get_path (file));
        load_users (user_list);
    }
}

static void
update_users (LightDMUserList *user_list)
{
    LightDMUserListPrivate *priv = GET_PRIVATE (user_list);
    GFile *passwd_file;
    GError *error = NULL;

    if (priv->have_users)
        return;

    load_users (user_list);

    /* Watch for changes to user list */
    passwd_file = g_file_new_for_path (PASSWD_FILE);
    priv->passwd_monitor = g_file_monitor (passwd_file, G_FILE_MONITOR_NONE, NULL, &error);
    g_object_unref (passwd_file);
    if (!priv->passwd_monitor)
        g_warning ("Error monitoring %s: %s", PASSWD_FILE, error->message);
    else
        g_signal_connect (priv->passwd_monitor, "changed", G_CALLBACK (passwd_changed_cb), user_list);
    g_clear_error (&error);

    priv->have_users = TRUE;
}

/**
 * lightdm_user_list_get_num_users:
 * @user_list: a #LightDMUserList
 *
 * Return value: The number of users able to log in
 **/
gint
lightdm_user_list_get_num_users (LightDMUserList *user_list)
{
    g_return_val_if_fail (LIGHTDM_IS_USER_LIST (user_list), 0);
    update_users (user_list);
    return g_list_length (GET_PRIVATE (user_list)->users);
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
    update_users (user_list);
    return GET_PRIVATE (user_list)->users;
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
    g_return_val_if_fail (LIGHTDM_IS_USER_LIST (user_list), NULL);
    g_return_val_if_fail (username != NULL, NULL);

    update_users (user_list);

    return get_user_by_name (user_list, username);
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

    switch (prop_id) {
    case PROP_NUM_USERS:
        g_value_set_int (value, lightdm_user_list_get_num_users (self));
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
    LightDMUserListPrivate *priv = GET_PRIVATE (self);

    if (priv->system_bus)
        g_object_unref (priv->system_bus);
    if (priv->passwd_monitor)
        g_object_unref (priv->passwd_monitor);
    g_list_free_full (priv->users, g_object_unref);

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
                                     PROP_NUM_USERS,
                                     g_param_spec_int ("num-users",
                                                       "num-users",
                                                       "Number of login users",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READABLE));
    /**
     * LightDMUserList::user-added:
     * @user_list: A #LightDMUserList
     * @user: The #LightDM user that has been added.
     *
     * The ::user-added signal gets emitted when a user account is created.
     **/
    signals[USER_ADDED] =
        g_signal_new ("user-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserListClass, user_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);

    /**
     * LightDMUserList::user-changed:
     * @user_list: A #LightDMUserList
     * @user: The #LightDM user that has been changed.
     *
     * The ::user-changed signal gets emitted when a user account is modified.
     **/
    signals[USER_CHANGED] =
        g_signal_new ("user-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserListClass, user_changed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);

    /**
     * LightDMUserList::user-removed:
     * @user_list: A #LightDMUserList
     * @user: The #LightDM user that has been removed.
     *
     * The ::user-removed signal gets emitted when a user account is removed.
     **/
    signals[USER_REMOVED] =
        g_signal_new ("user-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMUserListClass, user_removed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);
}
