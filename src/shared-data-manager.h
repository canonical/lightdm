/*
 * Copyright (C) 2014 Canonical, Ltd
 * Author: Michael Terry <michael.terry@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef SHARED_DATA_MANAGER_H_
#define SHARED_DATA_MANAGER_H_

#include <glib-object.h>

typedef struct SharedDataManager SharedDataManager;

G_BEGIN_DECLS

#define SHARED_DATA_MANAGER_TYPE (shared_data_manager_get_type())
#define SHARED_DATA_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SHARED_DATA_MANAGER_TYPE, SharedDataManager))
#define SHARED_DATA_MANAGER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), SHARED_DATA_MANAGER_TYPE, SharedDataManagerClass))
#define SHARED_DATA_MANAGER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SHARED_DATA_MANAGER_TYPE, SharedDataManagerClass))

struct SharedDataManager
{
    GObject parent_instance; // 管理对象的生命周期
};

typedef struct
{
    GObjectClass parent_class;
} SharedDataManagerClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SharedDataManager, g_object_unref)

GType shared_data_manager_get_type (void);

SharedDataManager *shared_data_manager_get_instance (void);

void shared_data_manager_start (SharedDataManager *manager);

void shared_data_manager_cleanup (void);

gchar *shared_data_manager_ensure_user_dir (SharedDataManager *manager, const gchar *user);

G_END_DECLS

#endif /* SHARED_DATA_MANAGER_H_ */
