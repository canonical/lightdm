/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*-
 *
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <pwd.h>
#include <stdlib.h>

#include "accounts.h"
#include "user-list.h"

struct UserPrivate
{
    /* Internal user object */
    CommonUser *common_user;
};

G_DEFINE_TYPE (User, user, G_TYPE_OBJECT);

User *
accounts_get_user_by_name (const gchar *username)
{
    User *user = NULL;
    CommonUser *common_user;

    g_return_val_if_fail (username != NULL, NULL);

    common_user = common_user_list_get_user_by_name (common_user_list_get_instance (), username);
    if (common_user != NULL)
    {
        user = g_object_new (USER_TYPE, NULL);
        user->priv->common_user = common_user;
    }

    return user;
}

User *
accounts_get_current_user ()
{
    struct passwd *entry = getpwuid (getuid ());
    if (entry != NULL)
        return accounts_get_user_by_name (entry->pw_name);
    else
        return NULL;
}

const gchar *
user_get_name (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_name (user->priv->common_user);
}

uid_t
user_get_uid (User *user)
{
    g_return_val_if_fail (user != NULL, 0);
    return common_user_get_uid (user->priv->common_user);
}

gid_t
user_get_gid (User *user)
{
    g_return_val_if_fail (user != NULL, 0);
    return common_user_get_gid (user->priv->common_user);
}

const gchar *
user_get_home_directory (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_home_directory (user->priv->common_user);
}

const gchar *
user_get_shell (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_shell (user->priv->common_user);
}

void
user_set_language (User *user, const gchar *language)
{
    g_return_if_fail (user != NULL);
    common_user_set_language (user->priv->common_user, language);
}

const gchar *
user_get_language (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_language (user->priv->common_user);
}

void
user_set_xsession (User *user, const gchar *xsession)
{
    g_return_if_fail (user != NULL);
    common_user_set_session (user->priv->common_user, xsession);
}

const gchar *
user_get_xsession (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_session (user->priv->common_user);
}

static void
user_init (User *user)
{
    user->priv = G_TYPE_INSTANCE_GET_PRIVATE (user, USER_TYPE, UserPrivate);
}

static void
user_dispose (GObject *object)
{
    User *self;

    self = USER (object);

    if (self->priv->common_user)
        g_object_unref (self->priv->common_user);
    self->priv->common_user = NULL;

    G_OBJECT_CLASS (user_parent_class)->dispose (object);
}

static void
user_class_init (UserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = user_dispose;

    g_type_class_add_private (klass, sizeof (UserPrivate));
}
