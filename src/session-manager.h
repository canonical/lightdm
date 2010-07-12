/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _SESSION_MANAGER_H_
#define _SESSION_MANAGER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define SESSION_MANAGER_TYPE (session_manager_get_type())
#define SESSION_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SESSION_MANAGER_TYPE, SessionManager));

typedef struct SessionManagerPrivate SessionManagerPrivate;

typedef struct
{
    GObject         parent_instance;
    SessionManagerPrivate *priv;
} SessionManager;

typedef struct
{
    GObjectClass parent_class;
} SessionManagerClass;

typedef struct
{
    char *key;
    char *name;
    char *comment;
    char *exec;
} SessionConfig;

GType session_manager_get_type (void);

SessionManager *session_manager_new (void);

SessionConfig *session_manager_get_session (SessionManager *manager, const gchar *key);

gboolean session_manager_get_sessions (SessionManager *manager, GPtrArray **sessions, GError *error);

G_END_DECLS

#endif /* _SESSION_MANAGER_H_ */
