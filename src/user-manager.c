/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <string.h>
#include <pwd.h>
#include <errno.h>
#include <gio/gio.h>

#include "user-manager.h"

enum {
    USER_ADDED,
    USER_UPDATED,
    USER_REMOVED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct UserManagerPrivate
{
    /* Configuration file */
    GKeyFile *config;
  
    /* File monitor for password file */
    GFileMonitor *passwd_monitor;

    /* TRUE if have scanned users */
    gboolean have_users;

    /* List of users */
    GList *users;
};

G_DEFINE_TYPE (UserManager, user_manager, G_TYPE_OBJECT);

UserManager *
user_manager_new (GKeyFile *config_file)
{
    UserManager *manager;

    manager = g_object_new (USER_MANAGER_TYPE, NULL);
    manager->priv->config = config_file;

    return manager;
}

static gint
compare_user (gconstpointer a, gconstpointer b)
{
    const UserInfo *user_a = a, *user_b = b;
    const gchar *name_a, *name_b;
    name_a = user_a->real_name ? user_a->real_name : user_a->name;
    name_b = user_b->real_name ? user_b->real_name : user_b->name;
    return strcmp (name_a, name_b);
}

static void
load_users (UserManager *manager)
{
    gchar **hidden_users, **hidden_shells;
    gchar *value;
    gint minimum_uid;
    GList *users = NULL, *old_users, *new_users = NULL, *updated_users = NULL, *link;

    if (g_key_file_has_key (manager->priv->config, "UserManager", "minimum-uid", NULL))
        minimum_uid = g_key_file_get_integer (manager->priv->config, "UserManager", "minimum-uid", NULL);
    else
        minimum_uid = 500;

    value = g_key_file_get_string (manager->priv->config, "UserManager", "hidden-users", NULL);
    if (!value)
        value = g_strdup ("nobody nobody4 noaccess");
    hidden_users = g_strsplit (value, " ", -1);
    g_free (value);

    value = g_key_file_get_string (manager->priv->config, "UserManager", "hidden-shells", NULL);
    if (!value)
        value = g_strdup ("/bin/false /usr/sbin/nologin");
    hidden_shells = g_strsplit (value, " ", -1);
    g_free (value);

    setpwent ();

    while (TRUE)
    {
        struct passwd *entry;
        UserInfo *user;
        char **tokens;
        gchar *image_path;
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

        user = g_malloc0 (sizeof (UserInfo));
        user->name = g_strdup (entry->pw_name);

        tokens = g_strsplit (entry->pw_gecos, ",", -1);
        if (tokens[0] != NULL && tokens[0][0] != '\0')
            user->real_name = g_strdup (tokens[0]);
        else
            user->real_name = NULL;
        g_strfreev (tokens);
      
        user->home_dir = g_strdup (entry->pw_dir);

        image_path = g_build_filename (user->home_dir, ".face", NULL);
        if (!g_file_test (image_path, G_FILE_TEST_EXISTS))
        {
            g_free (image_path);
            image_path = g_build_filename (user->home_dir, ".face.icon", NULL);
            if (!g_file_test (image_path, G_FILE_TEST_EXISTS))
            {
                g_free (image_path);
                image_path = NULL;
            }
        }
        if (image_path)
            user->image = g_filename_to_uri (image_path, NULL, NULL);
        else
            user->image = g_strdup ("");
        g_free (image_path);

        /* Update existing users if have them */
        for (link = manager->priv->users; link; link = link->next)
        {
            UserInfo *info = link->data;
            if (strcmp (info->name, user->name) == 0)
            {
                if (strcmp (info->real_name, user->real_name) != 0 ||
                    strcmp (info->image, user->image) != 0 ||
                    strcmp (info->home_dir, user->home_dir) != 0 ||
                    info->logged_in != user->logged_in)
                {
                    g_free (info->real_name);
                    g_free (info->image);
                    g_free (info->home_dir);
                    info->real_name = user->real_name;
                    info->image = user->image;
                    info->home_dir = user->home_dir;
                    info->logged_in = user->logged_in;
                    g_free (user);
                    user = info;
                    updated_users = g_list_insert_sorted (updated_users, user, compare_user);
                }
                else
                {
                    g_free (user->real_name);
                    g_free (user->image);
                    g_free (user->home_dir);
                    g_free (user);
                    user = info;
                }
                break;
            }
        }
        if (!link)
        {
            /* Only notify once we have loaded the user list */
            if (manager->priv->have_users)
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
    old_users = manager->priv->users;
    manager->priv->users = users;

    /* Notify of changes */
    for (link = new_users; link; link = link->next)
    {
        UserInfo *info = link->data;
        g_debug ("User %s added", info->name);
        g_signal_emit (manager, signals[USER_ADDED], 0, info);
    }
    g_list_free (new_users);
    for (link = updated_users; link; link = link->next)
    {
        UserInfo *info = link->data;
        g_debug ("User %s updated", info->name);
        g_signal_emit (manager, signals[USER_UPDATED], 0, info);
    }
    g_list_free (updated_users);
    for (link = old_users; link; link = link->next)
    {
        GList *new_link;

        /* See if this user is in the current list */
        for (new_link = manager->priv->users; new_link; new_link = new_link->next)
        {
            if (new_link->data == link->data)
                break;
        }

        if (!new_link)
        {
            UserInfo *info = link->data;
            g_debug ("User %s removed", info->name);
            g_signal_emit (manager, signals[USER_REMOVED], 0, info);
            g_free (info->name);
            g_free (info->real_name);
            g_free (info->image);
            g_free (info->home_dir);
            g_free (info);
        }
    }
    g_list_free (old_users);
}

static void
passwd_changed_cb (GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, UserManager *manager)
{
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
        g_debug ("%s changed, reloading user list", g_file_get_path (file));
        load_users (manager);
    }
}

static void
update_users (UserManager *manager)
{
    GFile *passwd_file;
    GError *error = NULL;

    if (manager->priv->have_users)
        return;

    /* User listing is disabled */
    if (g_key_file_has_key (manager->priv->config, "UserManager", "load-users", NULL) &&
        !g_key_file_get_boolean (manager->priv->config, "UserManager", "load-users", NULL))
    {
        manager->priv->have_users = TRUE;
        return;
    }

    load_users (manager);

    /* Watch for changes to user list */
    passwd_file = g_file_new_for_path ("/etc/passwd");
    manager->priv->passwd_monitor = g_file_monitor (passwd_file, G_FILE_MONITOR_NONE, NULL, &error);
    g_object_unref (passwd_file);
    if (!manager->priv->passwd_monitor)
        g_warning ("Error monitoring /etc/passwd: %s", error->message);
    else
        g_signal_connect (manager->priv->passwd_monitor, "changed", G_CALLBACK (passwd_changed_cb), manager);
    g_clear_error (&error);

    manager->priv->have_users = TRUE;
}

gint
user_manager_get_num_users (UserManager *manager)
{
    update_users (manager);
    return g_list_length (manager->priv->users);
}

const UserInfo *
user_manager_get_user (UserManager *manager, const gchar *username)
{
    GList *link;

    update_users (manager);

    for (link = manager->priv->users; link; link = link->next)
    {
        UserInfo *info = link->data;
        if (strcmp (info->name, username) == 0)
            return info;
    }

    return NULL;
}

GList *
user_manager_get_users (UserManager *manager)
{
    update_users (manager);
    return manager->priv->users;
}

gboolean
user_manager_get_user_defaults (UserManager *manager, gchar *username, gchar **language, gchar **layout, gchar **session)
{
    const UserInfo *info;
    GKeyFile *dmrc_file;
    gboolean have_dmrc;
    gchar *path;

    info = user_manager_get_user (manager, username);
    if (!info)
    {
        g_debug ("Unable to get user defaults, user %s does not exist", username);
        return FALSE;
    }

    dmrc_file = g_key_file_new ();
    g_key_file_set_string (dmrc_file, "Desktop", "Language", "");
    g_key_file_set_string (dmrc_file, "Desktop", "Layout", "");
    g_key_file_set_string (dmrc_file, "Desktop", "Session", "");

    /* Load the users login settings (~/.dmrc) */  
    path = g_build_filename (info->home_dir, ".dmrc", NULL);
    have_dmrc = g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_NONE, NULL);
    g_free (path);

    /* If no .dmrc, then load from the cache */
    if (!have_dmrc)
    {
        gchar *filename;

        filename = g_strdup_printf ("%s.dmrc", username);
        path = g_build_filename (CACHE_DIR, "dmrc", filename, NULL);
        g_free (filename);
        have_dmrc = g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_NONE, NULL);
        g_free (path);
    }

    *language = g_key_file_get_string (dmrc_file, "Desktop", "Language", NULL);
    *layout = g_key_file_get_string (dmrc_file, "Desktop", "Layout", NULL);
    *session = g_key_file_get_string (dmrc_file, "Desktop", "Session", NULL);

    g_key_file_free (dmrc_file);

    return TRUE;
}


static void
user_manager_init (UserManager *manager)
{
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, USER_MANAGER_TYPE, UserManagerPrivate);
}

static void
user_manager_class_init (UserManagerClass *klass)
{
    g_type_class_add_private (klass, sizeof (UserManagerPrivate));

    signals[USER_ADDED] =
        g_signal_new ("user-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (UserManagerClass, user_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    signals[USER_UPDATED] =
        g_signal_new ("user-updated",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (UserManagerClass, user_updated),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    signals[USER_REMOVED] =
        g_signal_new ("user-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (UserManagerClass, user_removed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
}
