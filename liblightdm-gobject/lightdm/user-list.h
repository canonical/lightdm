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

#ifndef _LIGHTDM_USER_LIST_H_
#define _LIGHTDM_USER_LIST_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LIGHTDM_TYPE_USER_LIST            (lightdm_user_list_get_type())
#define LIGHTDM_USER_LIST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIGHTDM_TYPE_USER_LIST, LightDMUserList));
#define LIGHTDM_USER_LIST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LIGHTDM_TYPE_USER_LIST, LightDMUserListClass))
#define LIGHTDM_IS_USER_LIST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIGHTDM_TYPE_USER_LIST))
#define LIGHTDM_IS_USER_LIST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LIGHTDM_TYPE_USER_LIST))
#define LIGHTDM_USER_LIST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LIGHTDM_TYPE_USER_LIST, LightDMUserListClass))

#include "user.h"

typedef struct
{
    GObject parent_instance;
} LightDMUserList;

typedef struct
{
    GObjectClass parent_class;

    void (*user_added)(LightDMUserList *user_list, LightDMUser *user);
    void (*user_changed)(LightDMUserList *user_list, LightDMUser *user);
    void (*user_removed)(LightDMUserList *user_list, LightDMUser *user);
} LightDMUserListClass;

GType lightdm_user_list_get_type (void);

LightDMUserList *lightdm_user_list_new (void);

gint lightdm_user_list_get_num_users (LightDMUserList *user_list);

LightDMUser *lightdm_user_list_get_user_by_name (LightDMUserList *user_list, const gchar *username);

GList *lightdm_user_list_get_users (LightDMUserList *user_list);

G_END_DECLS

#endif /* _LIGHTDM_USER_LIST_H_ */
