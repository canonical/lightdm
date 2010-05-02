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
  
    void (*show_prompt)(UserManager *manager, const gchar *text);
    void (*show_message)(UserManager *manager, const gchar *text);
    void (*show_error)(UserManager *manager, const gchar *text);
    void (*authentication_complete)(UserManager *manager);
} UserManagerClass;

typedef struct
{
   const char *name;
   const char *real_name;
}  UserInfo;

GType user_manager_get_type (void);

UserManager *user_manager_new (void);

gboolean user_manager_connect (UserManager *manager);

gint user_manager_get_num_users (UserManager *manager);

gboolean user_manager_get_users (UserManager *manager, GPtrArray **users, GError *error);

void user_manager_start_authentication (UserManager *manager, const char *username);

void user_manager_provide_secret (UserManager *manager, const gchar *secret);

void user_manager_cancel_authentication (UserManager *manager);

gboolean user_manager_get_is_authenticated (UserManager *manager);

G_END_DECLS

#endif /* _USER_MANAGER_H_ */
