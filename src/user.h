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

#ifndef _USER_H_
#define _USER_H_

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

GType user_get_type (void);

void user_set_use_pam (void);

void user_set_use_passwd_file (gchar *passwd_file);

User *user_get_by_name (const gchar *username);

User *user_get_by_uid (uid_t uid);

User *user_get_current (void);

const gchar *user_get_name (User *user);

uid_t user_get_uid (User *user);

gid_t user_get_gid (User *user);

const gchar *user_get_gecos (User *user);

const gchar *user_get_home_directory (User *user);

const gchar *user_get_shell (User *user);

gchar *user_get_xsession (User *user);

void user_set_xsession (User *user, const gchar *session);

G_END_DECLS

#endif /* _USER_H_ */
