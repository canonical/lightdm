/*
 * Copyright (C) 2011 Canonical Ltd
 * Author: Michael Terry <michael.terry@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _ACCOUNTS_H_
#define _ACCOUNTS_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define ACCOUNTS_TYPE (accounts_get_type())
#define ACCOUNTS(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), ACCOUNTS_TYPE, Accounts));

typedef struct AccountsPrivate AccountsPrivate;

typedef struct
{
    GObject          parent_instance;
    AccountsPrivate *priv;
} Accounts;

typedef struct
{
    GObjectClass parent_class;
} AccountsClass;

GType accounts_get_type (void);

Accounts *accounts_new (const gchar *user);

void accounts_set_session (Accounts *accounts, const gchar *section);

gchar *accounts_get_session (Accounts *accounts);

G_END_DECLS

#endif /* _ACCOUNTS_H_ */
