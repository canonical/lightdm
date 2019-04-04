/*
 * Copyright (C) 2010 Robert Ancell.
 * Copyright (C) 2014 Canonical, Ltd.
 * Authors: Robert Ancell <robert.ancell@canonical.com>
 *          Michael Terry <michael.terry@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef COMMON_USER_LIST_H_
#define COMMON_USER_LIST_H_

#include <glib-object.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define COMMON_TYPE_USER_LIST            (common_user_list_get_type())
#define COMMON_USER_LIST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COMMON_TYPE_USER_LIST, CommonUserList));
#define COMMON_USER_LIST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COMMON_TYPE_USER_LIST, CommonUserListClass))
#define COMMON_IS_USER_LIST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COMMON_TYPE_USER_LIST))
#define COMMON_IS_USER_LIST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COMMON_TYPE_USER_LIST))
#define COMMON_USER_LIST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COMMON_TYPE_USER_LIST, CommonUserListClass))

#define COMMON_TYPE_USER            (common_user_get_type())
#define COMMON_USER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COMMON_TYPE_USER, CommonUser));
#define COMMON_USER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), COMMON_TYPE_USER, CommonUserClass))
#define COMMON_IS_USER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COMMON_TYPE_USER))
#define COMMON_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), COMMON_TYPE_USER))
#define COMMON_USER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), COMMON_TYPE_USER, CommonUserClass))

#define USER_LIST_SIGNAL_USER_ADDED   "user-added"
#define USER_LIST_SIGNAL_USER_CHANGED "user-changed"
#define USER_LIST_SIGNAL_USER_REMOVED "user-removed"

#define USER_SIGNAL_CHANGED "changed"

typedef struct
{
    GObject parent_instance;
} CommonUser;

typedef struct
{
    GObjectClass parent_class;

    void (*changed)(CommonUser *user);
} CommonUserClass;

typedef struct
{
    GObject parent_instance;
} CommonUserList;

typedef struct
{
    GObjectClass parent_class;

    void (*user_added)(CommonUserList *user_list, CommonUser *user);
    void (*user_changed)(CommonUserList *user_list, CommonUser *user);
    void (*user_removed)(CommonUserList *user_list, CommonUser *user);
} CommonUserListClass;

GType common_user_list_get_type (void);

GType common_user_get_type (void);

CommonUserList *common_user_list_get_instance (void);

void common_user_list_cleanup (void);

gint common_user_list_get_length (CommonUserList *user_list);

CommonUser *common_user_list_get_user_by_name (CommonUserList *user_list, const gchar *username);

GList *common_user_list_get_users (CommonUserList *user_list);

const gchar *common_user_get_name (CommonUser *user);

const gchar *common_user_get_real_name (CommonUser *user);

const gchar *common_user_get_display_name (CommonUser *user);

const gchar *common_user_get_home_directory (CommonUser *user);

const gchar *common_user_get_shell (CommonUser *user);

const gchar *common_user_get_image (CommonUser *user);

const gchar *common_user_get_background (CommonUser *user);

const gchar *common_user_get_language (CommonUser *user);

void common_user_set_language (CommonUser *user, const gchar *language);

const gchar *common_user_get_layout (CommonUser *user);

const gchar * const *common_user_get_layouts (CommonUser *user);

const gchar *common_user_get_session (CommonUser *user);

void common_user_set_session (CommonUser *user, const gchar *session);

gboolean common_user_get_logged_in (CommonUser *user);

gboolean common_user_get_has_messages (CommonUser *user);

uid_t common_user_get_uid (CommonUser *user);

gid_t common_user_get_gid (CommonUser *user);

gboolean common_user_get_is_locked (CommonUser *user);

G_END_DECLS

#endif /* COMMON_USER_LIST_H_ */
