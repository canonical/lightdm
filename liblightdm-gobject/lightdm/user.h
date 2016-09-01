/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef LIGHTDM_USER_H_
#define LIGHTDM_USER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LIGHTDM_TYPE_USER (lightdm_user_get_type())

G_DECLARE_FINAL_TYPE (LightDMUser, lightdm_user, LIGHTDM, USER, GObject)

#define LIGHTDM_SIGNAL_USER_CHANGED "changed"

struct _LightDMUserClass
{
    /*< private >*/
    GObjectClass parent_class;
};

#define LIGHTDM_TYPE_USER_LIST (lightdm_user_list_get_type())

G_DECLARE_FINAL_TYPE (LightDMUserList, lightdm_user_list, LIGHTDM, USER_LIST, GObject)

#define LIGHTDM_USER_LIST_SIGNAL_USER_ADDED   "user-added"
#define LIGHTDM_USER_LIST_SIGNAL_USER_CHANGED "user-changed"
#define LIGHTDM_USER_LIST_SIGNAL_USER_REMOVED "user-removed"

struct _LightDMUserListClass
{
    /*< private >*/
    GObjectClass parent_class;
};

LightDMUserList *lightdm_user_list_get_instance (void);

gint lightdm_user_list_get_length (LightDMUserList *user_list);

LightDMUser *lightdm_user_list_get_user_by_name (LightDMUserList *user_list, const gchar *username);

GList *lightdm_user_list_get_users (LightDMUserList *user_list);

const gchar *lightdm_user_get_name (LightDMUser *user);

const gchar *lightdm_user_get_real_name (LightDMUser *user);

const gchar *lightdm_user_get_display_name (LightDMUser *user);

const gchar *lightdm_user_get_home_directory (LightDMUser *user);

const gchar *lightdm_user_get_image (LightDMUser *user);

const gchar *lightdm_user_get_background (LightDMUser *user);

const gchar *lightdm_user_get_language (LightDMUser *user);

const gchar *lightdm_user_get_layout (LightDMUser *user);

const gchar * const *lightdm_user_get_layouts (LightDMUser *user);

const gchar *lightdm_user_get_session (LightDMUser *user);

gboolean lightdm_user_get_logged_in (LightDMUser *user);

gboolean lightdm_user_get_has_messages (LightDMUser *user);

uid_t lightdm_user_get_uid (LightDMUser *user);

G_END_DECLS

#endif /* LIGHTDM_USER_H_ */
