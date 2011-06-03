/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _GUEST_MANAGER_H_
#define _GUEST_MANAGER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define GUEST_MANAGER_TYPE (guest_manager_get_type())
#define GUEST_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GUEST_MANAGER_TYPE, GuestManager))

typedef struct GuestManagerPrivate GuestManagerPrivate;

typedef struct
{
    GObject              parent_instance;
    GuestManagerPrivate *priv;  
} GuestManager;

typedef struct
{
    GObjectClass parent_class;
} GuestManagerClass;

GType guest_manager_get_type (void);

GuestManager *guest_manager_new (GKeyFile *config);

gboolean guest_manager_get_is_enabled (GuestManager *manager);

gchar *guest_manager_add_account (GuestManager *manager);

void guest_manager_remove_account (GuestManager *manager, const gchar *username);

G_END_DECLS

#endif /* _GUEST_MANAGER_H_ */
