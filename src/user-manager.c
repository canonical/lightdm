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

#include "user-manager.h"
#include "user-manager-glue.h"

struct UserManagerPrivate
{
    gboolean have_users;
    GList *users;
};

G_DEFINE_TYPE (UserManager, user_manager, G_TYPE_OBJECT);

UserManager *
user_manager_new (/*int argc, char **argv*/)
{
    return g_object_new (USER_MANAGER_TYPE, NULL);
}

// FIXME: 100 or 500? Make it configurable
#define MINIMUM_UID 500

static gint
compare_user (gconstpointer a, gconstpointer b)
{
    const UserInfo *user_a = a, *user_b = b;
    const char *name_a, *name_b;
    name_a = user_a->real_name ? user_a->real_name : user_a->name;
    name_b = user_b->real_name ? user_b->real_name : user_b->name;
    return strcmp (name_a, name_b);
}

static void
update_users (UserManager *manager)
{
    char *invalid_shells[] = { "/bin/false", "/usr/sbin/nologin", NULL };
    char *invalid_users[] = { "nobody", NULL };

    if (manager->priv->have_users)
        return;

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
        if (entry->pw_uid < MINIMUM_UID)
            continue;

        /* Ignore users disabled by shell */
        if (entry->pw_shell)
        {
            for (i = 0; invalid_shells[i] && strcmp (entry->pw_shell, invalid_shells[i]) != 0; i++);
            if (invalid_shells[i])
                continue;
        }

        /* Ignore certain users */
        for (i = 0; invalid_users[i] && strcmp (entry->pw_name, invalid_users[i]) != 0; i++);
        if (invalid_users[i])
            continue;

        user = g_malloc0 (sizeof (UserInfo));
        user->name = g_strdup (entry->pw_name);

        tokens = g_strsplit (entry->pw_gecos, ",", -1);
        if (tokens[0] != NULL && tokens[0][0] != '\0')
            user->real_name = g_strdup (tokens[0]);
        else
            user->real_name = NULL;
        g_strfreev (tokens);

        image_path = g_build_filename ("/home", user->name, ".face", NULL);
        if (g_file_test (image_path, G_FILE_TEST_EXISTS))
            user->image = g_filename_to_uri (image_path, NULL, NULL);
        else
            user->image = g_strdup ("");
        g_free (image_path);

        manager->priv->users = g_list_insert_sorted (manager->priv->users, user, compare_user);
    }

    if (errno != 0)
        g_warning ("Failed to read password database: %s", strerror (errno));

    endpwent ();

    manager->priv->have_users = TRUE;
}

gint
user_manager_get_num_users (UserManager *manager)
{
    update_users (manager);
    return g_list_length (manager->priv->users);
}

#define TYPE_USER dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID)

gboolean
user_manager_get_users (UserManager *manager, GPtrArray **users, GError *error)
{
    GList *link;

    update_users (manager);

    *users = g_ptr_array_sized_new (g_list_length (manager->priv->users));
    for (link = manager->priv->users; link; link = link->next)
    {
        UserInfo *info = link->data;
        GValue value = { 0 };

        g_value_init (&value, TYPE_USER);
        g_value_take_boxed (&value, dbus_g_type_specialized_construct (TYPE_USER));
        dbus_g_type_struct_set (&value, 0, info->name, 1, info->real_name, 2, info->image, 3, info->logged_in, G_MAXUINT);
        g_ptr_array_add (*users, g_value_get_boxed (&value));
    }

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

    dbus_g_object_type_install_info (USER_MANAGER_TYPE, &dbus_glib_user_manager_object_info);
}
