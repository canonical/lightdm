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

typedef struct
{
    /* Internal user object */
    CommonUser *common_user;
} UserPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (User, user, G_TYPE_OBJECT)

User *
accounts_get_user_by_name (const gchar *username)
{
    g_return_val_if_fail (username != NULL, NULL);

    CommonUser *common_user = common_user_list_get_user_by_name (common_user_list_get_instance (), username);
    if (common_user == NULL)
        return NULL;

    User *user = g_object_new (USER_TYPE, NULL);
    UserPrivate *priv = user_get_instance_private (user);
    priv->common_user = common_user;
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
    UserPrivate *priv = user_get_instance_private (user);
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_name (priv->common_user);
}

uid_t
user_get_uid (User *user)
{
    UserPrivate *priv = user_get_instance_private (user);
    g_return_val_if_fail (user != NULL, 0);
    return common_user_get_uid (priv->common_user);
}

gid_t
user_get_gid (User *user)
{
    UserPrivate *priv = user_get_instance_private (user);
    g_return_val_if_fail (user != NULL, 0);
    return common_user_get_gid (priv->common_user);
}

const gchar *
user_get_home_directory (User *user)
{
    UserPrivate *priv = user_get_instance_private (user);
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_home_directory (priv->common_user);
}

const gchar *
user_get_shell (User *user)
{
    UserPrivate *priv = user_get_instance_private (user);
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_shell (priv->common_user);
}

void
user_set_language (User *user, const gchar *language)
{
    UserPrivate *priv = user_get_instance_private (user);
    g_return_if_fail (user != NULL);
    common_user_set_language (priv->common_user, language);
}

const gchar *
user_get_language (User *user)
{
    UserPrivate *priv = user_get_instance_private (user);
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_language (priv->common_user);
}

void
user_set_xsession (User *user, const gchar *xsession)
{
    UserPrivate *priv = user_get_instance_private (user);
    g_return_if_fail (user != NULL);
    common_user_set_session (priv->common_user, xsession);
}

const gchar *
user_get_xsession (User *user)
{
    UserPrivate *priv = user_get_instance_private (user);
    g_return_val_if_fail (user != NULL, NULL);
    return common_user_get_session (priv->common_user);
}

static void
user_init (User *user)
{
}

static void
user_finalize (GObject *object)
{
    User *self = USER (object);
    UserPrivate *priv = user_get_instance_private (self);

    g_clear_object (&priv->common_user);

    G_OBJECT_CLASS (user_parent_class)->finalize (object);
}

static void
user_class_init (UserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = user_finalize;
}
