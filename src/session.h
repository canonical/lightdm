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

#include <glib-object.h>

#include <security/pam_appl.h>

#include "accounts.h"
#include "xauthority.h"

G_BEGIN_DECLS

#define SESSION_TYPE           (session_get_type())
#define SESSION(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), SESSION_TYPE, Session))
#define SESSION_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), SESSION_TYPE, SessionClass))
#define SESSION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SESSION_TYPE, SessionClass))

typedef struct SessionPrivate SessionPrivate;

typedef struct
{
    GObject         parent_instance;
    SessionPrivate *priv;
} Session;

typedef struct
{
    GObjectClass parent_class;

    void (*got_messages)(Session *session);
    void (*authentication_complete)(Session *session);
    void (*stopped)(Session *session);
} SessionClass;

typedef enum
{
    SESSION_TYPE_LOCAL,
    SESSION_TYPE_REMOTE
} SessionType;

#define XDG_SESSION_CLASS_USER        "user"
#define XDG_SESSION_CLASS_GREETER     "greeter"
#define XDG_SESSION_CLASS_LOCK_SCREEN "lock-screen"

GType session_get_type (void);

void session_set_log_file (Session *session, const gchar *filename);

void session_set_class (Session *session, const gchar *class);

void session_set_tty (Session *session, const gchar *tty);

void session_set_xdisplay (Session *session, const gchar *xdisplay);

void session_set_xauthority (Session *session, XAuthority *authority, gboolean use_system_location);

void session_set_remote_host_name (Session *session, const gchar *remote_host_name);

void session_set_env (Session *session, const gchar *name, const gchar *value);

// FIXME: Remove
User *session_get_user (Session *session);

gboolean session_start (Session *session, const gchar *service, const gchar *username, gboolean do_authenticate, gboolean is_interactive, gboolean is_guest);

const gchar *session_get_username (Session *session);

const gchar *session_get_console_kit_cookie (Session *session);

void session_respond (Session *session, struct pam_response *response);

void session_respond_error (Session *session, int error);

int session_get_messages_length (Session *session);

const struct pam_message *session_get_messages (Session *session);

gboolean session_get_is_authenticated (Session *session);

int session_get_authentication_result (Session *session);

const gchar *session_get_authentication_result_string (Session *session);

void session_run (Session *session, gchar **argv);

void session_lock (Session *session);

void session_unlock (Session *session);

void session_stop (Session *session);

gboolean session_get_is_stopped (Session *session);

G_END_DECLS

#endif /* _SESSION_H_ */
