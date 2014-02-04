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

#ifndef USER_H_
#define USER_H_

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define USER_TYPE (user_get_type())
#define USER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), USER_TYPE, User));

typedef struct UserPrivate UserPrivate;

typedef struct
{
    GObject      parent_instance;
    UserPrivate *priv;
} User;

typedef struct
{
    GObjectClass parent_class;
} UserClass;

User *accounts_get_user_by_name (const gchar *username);

User *accounts_get_current_user (void);

GType user_get_type (void);

const gchar *user_get_name (User *user);

uid_t user_get_uid (User *user);

gid_t user_get_gid (User *user);

const gchar *user_get_home_directory (User *user);

const gchar *user_get_shell (User *user);

const gchar *user_get_xsession (User *user);

void user_set_xsession (User *user, const gchar *session);

const gchar *user_get_language (User *user);

void user_set_language (User *user, const gchar *language);

G_END_DECLS

#endif /* USER_H_ */
