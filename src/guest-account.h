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

gboolean guest_account_is_installed (void);

gchar *guest_account_setup (void);

void guest_account_cleanup (const gchar *username);

G_END_DECLS

#endif /* _GUEST_ACCOUNT_H_ */
