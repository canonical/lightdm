/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"

struct UserPrivate
{
    /* Name of user */
    gchar *name;

    /* User ID */
    uid_t uid;

    /* Group ID */
    gid_t gid;

    /* GECOS information */
    gchar *gecos;

    /* Home directory */
    gchar *home_directory;

    /* Shell */
    gchar *shell;
};

G_DEFINE_TYPE (User, user, G_TYPE_OBJECT);

static gboolean use_fake_users = FALSE;
static GList *fake_users = NULL;

static User *
user_from_passwd (struct passwd *user_info)
{
    User *user;

    user = g_object_new (USER_TYPE, NULL);
    user->priv->name = g_strdup (user_info->pw_name);
    user->priv->uid = user_info->pw_uid;
    user->priv->gid = user_info->pw_gid;
    user->priv->gecos = g_strdup (user_info->pw_gecos);
    user->priv->home_directory = g_strdup (user_info->pw_dir);
    user->priv->shell = g_strdup (user_info->pw_shell);

    return user;
}

void
user_set_use_passwd_file (gchar *passwd_file)
{
    gchar *data = NULL, **lines;
    gint i;
    GError *error = NULL;

    use_fake_users = TRUE;

    if (!g_file_get_contents (passwd_file, &data, NULL, &error))
        g_warning ("Error loading passwd file: %s", error->message);
    g_clear_error (&error);
  
    if (!data)
        return;

    lines = g_strsplit (data, "\n", -1);
    g_free (data);

    for (i = 0; lines[i]; i++)
    {
        gchar *line, **fields;

        line = g_strstrip (lines[i]);
        fields = g_strsplit (line, ":", -1);
        if (g_strv_length (fields) == 7)
        {
            User *user = g_object_new (USER_TYPE, NULL);
            user->priv->name = g_strdup (fields[0]);
            user->priv->uid = atoi (fields[2]);
            user->priv->gid = atoi (fields[3]);
            user->priv->gecos = g_strdup (fields[4]);
            user->priv->home_directory = g_strdup (fields[5]);
            user->priv->shell = g_strdup (fields[6]);
            fake_users = g_list_append (fake_users, user);
        }
        g_strfreev (fields);
    }
    g_strfreev (lines);
}

User *
user_get_by_name (const gchar *username)
{
    User *user = NULL;

    errno = 0;
    if (use_fake_users)
    {
        GList *iter;
        for (iter = fake_users; iter; iter = iter->next)
        {
            User *u = iter->data;
            if (strcmp (u->priv->name, username) == 0)
            {
                user = g_object_ref (u);
                break;
            }
        }
    }
    else
    {
        struct passwd *user_info;

        user_info = getpwnam (username);
        if (user_info)
            user = user_from_passwd (user_info);
    }

    if (!user)
    {
        if (errno == 0)
            g_warning ("Unable to get information on user %s: User does not exist", username);
        else
            g_warning ("Unable to get information on user %s: %s", username, strerror (errno));
    }

    return user;
}

User *
user_get_by_uid (uid_t uid)
{
    User *user = NULL;

    errno = 0;
    if (use_fake_users)
    {
        GList *iter;
        for (iter = fake_users; iter; iter = iter->next)
        {
            User *u = iter->data;
            if (u->priv->uid == uid)
            {
                user = g_object_ref (u);
                break;
            }
        }
    }
    else
    {
        struct passwd *user_info;

        user_info = getpwuid (uid);
        if (user_info)
            user = user_from_passwd (user_info);
    }

    if (!user)
    {
        if (errno == 0)
            g_warning ("Unable to get information on user %d: User does not exist", uid);
        else
            g_warning ("Unable to get information on user %d: %s", uid, strerror (errno));
    }

    return user;
}

User *
user_get_current ()
{
    return user_from_passwd (getpwuid (getuid ()));
}

const gchar *
user_get_name (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return user->priv->name;
}

uid_t
user_get_uid (User *user)
{
    g_return_val_if_fail (user != NULL, 0);
    return user->priv->uid;
}

gid_t
user_get_gid (User *user)
{
    g_return_val_if_fail (user != NULL, 0);
    return user->priv->gid;
}

const gchar *
user_get_gecos (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return user->priv->gecos;
}

const gchar *
user_get_home_directory (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return user->priv->home_directory;
}

const gchar *
user_get_shell (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return user->priv->shell;
}

static void
user_init (User *user)
{
    user->priv = G_TYPE_INSTANCE_GET_PRIVATE (user, USER_TYPE, UserPrivate);
}

static void
user_finalize (GObject *object)
{
    User *self;

    self = USER (object);

    g_free (self->priv->name);
    g_free (self->priv->gecos);
    g_free (self->priv->home_directory);
    g_free (self->priv->shell);

    G_OBJECT_CLASS (user_parent_class)->finalize (object);  
}

static void
user_class_init (UserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = user_finalize;  

    g_type_class_add_private (klass, sizeof (UserPrivate));
}
