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

#ifndef _SYSTEMD_LOGIND_H_
#define _SYSTEMD_LOGIND_H_

#include <glib-object.h>

G_BEGIN_DECLS

gchar *logind_get_session_id (void);

void logind_lock_session (const gchar *session_path);

void logind_unlock_session (const gchar *session_path);

G_END_DECLS

#endif /* _SYSTEMD_LOGIND_H_ */
