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

#ifndef _SESSION_H_
#define _SESSION_H_

#include <glib-object.h>
#include "xauth.h"

G_BEGIN_DECLS

#define SESSION_TYPE (session_get_type())
#define SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SESSION_TYPE, Session));

typedef struct SessionPrivate SessionPrivate;

typedef struct
{
    GObject         parent_instance;
    SessionPrivate *priv;
} Session;

typedef struct
{
    GObjectClass parent_class;

    void (*exited) (Session *session, int status);
    void (*killed) (Session *session, int signum);
} SessionClass;

GType session_get_type (void);

Session *session_new (const char *username, const char *command);

const gchar *session_get_username (Session *session);

const gchar *session_get_command (Session *session);

void session_set_env (Session *session, const gchar *name, const gchar *value);

void session_set_authorization (Session *session, XAuthorization *authorization, const gchar *path);

XAuthorization *session_get_authorization (Session *session);

gboolean session_start (Session *session);

G_END_DECLS

#endif /* _SESSION_H_ */
