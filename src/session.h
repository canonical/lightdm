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

#ifndef SESSION_H_
#define SESSION_H_

#include <glib-object.h>

#include <security/pam_appl.h>

typedef struct Session Session;

#include "session-config.h"
#include "display-server.h"
#include "accounts.h"
#include "x-authority.h"
#include "logger.h"

G_BEGIN_DECLS

#define SESSION_TYPE           (session_get_type())
#define SESSION(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), SESSION_TYPE, Session))
#define SESSION_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), SESSION_TYPE, SessionClass))
#define SESSION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SESSION_TYPE, SessionClass))

#define SESSION_SIGNAL_GOT_MESSAGES            "got-messages"
#define SESSION_SIGNAL_AUTHENTICATION_COMPLETE "authentication-complete"
#define SESSION_SIGNAL_STOPPED                 "stopped"

typedef struct SessionPrivate SessionPrivate;

struct Session
{
    GObject         parent_instance;
    SessionPrivate *priv;
};

typedef struct
{
    GObjectClass parent_class;

    gboolean (*start)(Session *session);
    void (*run)(Session *session);
    void (*stop)(Session *session);

    void (*got_messages)(Session *session);
    void (*authentication_complete)(Session *session);
    void (*stopped)(Session *session);
} SessionClass;

typedef enum
{
    SESSION_TYPE_LOCAL,
    SESSION_TYPE_REMOTE
} SessionType;

GType session_get_type (void);

Session *session_new (void);

void session_set_config (Session *session, SessionConfig *config);

SessionConfig *session_get_config (Session *session);

const gchar *session_get_session_type (Session *session);

void session_set_pam_service (Session *session, const gchar *pam_service);

void session_set_username (Session *session, const gchar *username);

void session_set_do_authenticate (Session *session, gboolean do_authenticate);

void session_set_is_interactive (Session *session, gboolean is_interactive);

void session_set_is_guest (Session *session, gboolean is_guest);

gboolean session_get_is_guest (Session *session);

void session_set_log_file (Session *session, const gchar *filename);

void session_set_display_server (Session *session, DisplayServer *display_server);

DisplayServer *session_get_display_server (Session *session);

void session_set_tty (Session *session, const gchar *tty);

void session_set_xdisplay (Session *session, const gchar *xdisplay);

void session_set_x_authority (Session *session, XAuthority *authority, gboolean use_system_location);

void session_set_remote_host_name (Session *session, const gchar *remote_host_name);

void session_set_env (Session *session, const gchar *name, const gchar *value);

const gchar *session_get_env (Session *session, const gchar *name);

void session_unset_env (Session *session, const gchar *name);

void session_set_argv (Session *session, gchar **argv);

// FIXME: Remove
User *session_get_user (Session *session);

gboolean session_start (Session *session);

gboolean session_get_is_started (Session *session);

const gchar *session_get_username (Session *session);

const gchar *session_get_console_kit_cookie (Session *session);

void session_respond (Session *session, struct pam_response *response);

void session_respond_error (Session *session, int error);

int session_get_messages_length (Session *session);

const struct pam_message *session_get_messages (Session *session);

gboolean session_get_is_authenticated (Session *session);

int session_get_authentication_result (Session *session);

const gchar *session_get_authentication_result_string (Session *session);

void session_run (Session *session);

void session_lock (Session *session);

void session_unlock (Session *session);

void session_activate (Session *session);

void session_stop (Session *session);

gboolean session_get_is_stopping (Session *session);

G_END_DECLS

#endif /* SESSION_H_ */
