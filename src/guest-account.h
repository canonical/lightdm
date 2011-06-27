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

#ifndef _GUEST_ACCOUNT_H_
#define _GUEST_ACCOUNT_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define GUEST_ACCOUNT_TYPE (guest_account_get_type())
#define GUEST_ACCOUNT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GUEST_ACCOUNT_TYPE, GuestAccount))

typedef struct GuestAccountPrivate GuestAccountPrivate;

typedef struct
{
    GObject              parent_instance;
    GuestAccountPrivate *priv;  
} GuestAccount;

typedef struct
{
    GObjectClass parent_class;
} GuestAccountClass;

GType guest_account_get_type (void);

GuestAccount *guest_account_get_instance (void);

gboolean guest_account_get_is_enabled (GuestAccount *account);

const gchar *guest_account_get_username (GuestAccount *account);

gboolean guest_account_ref (GuestAccount *account);

void guest_account_unref (GuestAccount *account);

G_END_DECLS

#endif /* _GUEST_ACCOUNT_H_ */
