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

#ifndef _SESSION_H_
#define _SESSION_H_

#include "child-process.h"
#include "user.h"
#include "xauth.h"

G_BEGIN_DECLS

#define SESSION_TYPE (session_get_type())
#define SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SESSION_TYPE, Session))

typedef struct SessionPrivate SessionPrivate;

typedef struct
{
    ChildProcess    parent_instance;
    SessionPrivate *priv;
} Session;

typedef struct
{
    ChildProcessClass parent_class;
} SessionClass;

GType session_get_type (void);

Session *session_new (void);

void session_set_user (Session *session, User *user);

User *session_get_user (Session *session);

void session_set_command (Session *session, const gchar *command);

const gchar *session_get_command (Session *session);

void session_set_authorization (Session *session, XAuthorization *authorization);

XAuthorization *session_get_authorization (Session *session);

gboolean session_start (Session *session, gboolean create_pipe);

void session_stop (Session *session);

G_END_DECLS

#endif /* _SESSION_H_ */
