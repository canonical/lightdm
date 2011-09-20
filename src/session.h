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

#include "process.h"
#include "accounts.h"
#include "pam-session.h"

G_BEGIN_DECLS

#define SESSION_TYPE           (session_get_type())
#define SESSION(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), SESSION_TYPE, Session))
#define SESSION_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), SESSION_TYPE, SessionClass))
#define SESSION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SESSION_TYPE, SessionClass))

typedef struct SessionPrivate SessionPrivate;

typedef struct
{
    Process         parent_instance;
    SessionPrivate *priv;
} Session;

typedef struct
{
    ProcessClass parent_class;

    gboolean (*start)(Session *session);
    void     (*stop)(Session *session);
} SessionClass;

GType session_get_type (void);

void session_set_authentication (Session *session, PAMSession *authentication);

PAMSession *session_get_authentication (Session *session);

User *session_get_user (Session *session);

void session_set_is_greeter (Session *session, gboolean is_greeter);

gboolean session_get_is_greeter (Session *session);

void session_set_command (Session *session, const gchar *command);

const gchar *session_get_command (Session *session);

void session_set_console_kit_parameter (Session *session, const gchar *name, GVariant *value);

gboolean session_start (Session *session);

void session_unlock (Session *session);

void session_stop (Session *session);

G_END_DECLS

#endif /* _SESSION_H_ */
