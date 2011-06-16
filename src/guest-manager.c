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

#include "guest-manager.h"

struct GuestManagerPrivate
{
};

G_DEFINE_TYPE (GuestManager, guest_manager, G_TYPE_OBJECT);

static GuestManager *guest_manager_instance = NULL;

GuestManager *
guest_manager_get_instance ()
{
    if (!guest_manager_instance)
        guest_manager_instance = g_object_new (GUEST_MANAGER_TYPE, NULL);
    return guest_manager_instance;
}

gboolean
guest_manager_get_is_enabled (GuestManager *manager)
{
    return FALSE;
}

gchar *
guest_manager_add_account (GuestManager *manager)
{
    return NULL;
}

void
guest_manager_remove_account (GuestManager *manager, const gchar *username)
{
}

static void
guest_manager_init (GuestManager *manager)
{
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, GUEST_MANAGER_TYPE, GuestManagerPrivate);  
}

static void
guest_manager_finalize (GObject *object)
{
    //GuestManager *self = GUEST_MANAGER (object);

    G_OBJECT_CLASS (guest_manager_parent_class)->finalize (object);
}

static void
guest_manager_class_init (GuestManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = guest_manager_finalize;

    g_type_class_add_private (klass, sizeof (GuestManagerPrivate));
}
