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

#ifndef _USER_MANAGER_H_
#define _USER_MANAGER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define USER_MANAGER_TYPE (user_manager_get_type())
#define USER_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), USER_MANAGER_TYPE, UserManager));

typedef struct UserManagerPrivate UserManagerPrivate;

typedef struct
{
    GObject         parent_instance;
    UserManagerPrivate *priv;
} UserManager;

typedef struct
{
    GObjectClass parent_class;
} UserManagerClass;

typedef struct
{
    const gchar *name;
    const gchar *real_name;
    const gchar *image;
    gboolean logged_in;
} UserInfo;

GType user_manager_get_type (void);

UserManager *user_manager_new (void);

gint user_manager_get_num_users (UserManager *manager);

gboolean user_manager_get_users (UserManager *manager, GPtrArray **users, GError *error);

G_END_DECLS

#endif /* _USER_MANAGER_H_ */
