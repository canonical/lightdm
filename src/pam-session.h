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

#ifndef _PAM_SESSION_H_
#define _PAM_SESSION_H_

#include <glib-object.h>
#include <security/pam_appl.h>

#include "user.h"

G_BEGIN_DECLS

#define PAM_SESSION_TYPE (pam_session_get_type())
#define PAM_SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PAM_SESSION_TYPE, PAMSession));

typedef struct PAMSessionPrivate PAMSessionPrivate;

typedef struct
{
    GObject         parent_instance;
    PAMSessionPrivate *priv;
} PAMSession;

typedef struct
{
    GObjectClass parent_class;

    void (*authentication_started)(PAMSession *pam_session);  
    void (*got_messages)(PAMSession *pam_session, int num_msg, const struct pam_message **msg);
    void (*authentication_result)(PAMSession *pam_session, int result);
    void (*started)(PAMSession *pam_session);
} PAMSessionClass;

GType pam_session_get_type (void);

void pam_session_set_use_pam (void);

void pam_session_set_use_passwd_file (gchar *passwd_file);

PAMSession *pam_session_new (const gchar *service, User *user);

gboolean pam_session_authenticate (PAMSession *session, GError **error);

gboolean pam_session_get_is_authenticated (PAMSession *session);

gboolean pam_session_open (PAMSession *session);

gboolean pam_session_get_in_session (PAMSession *session);

const gchar *pam_session_strerror (PAMSession *session, int error);

User *pam_session_get_user (PAMSession *session);

const struct pam_message **pam_session_get_messages (PAMSession *session);

gint pam_session_get_num_messages (PAMSession *session);

void pam_session_respond (PAMSession *session, struct pam_response *response);

void pam_session_cancel (PAMSession *session);

const gchar *pam_session_getenv (PAMSession *session, const gchar *name);

gchar **pam_session_get_envlist(PAMSession *session);

void pam_session_close (PAMSession *session);

G_END_DECLS

#endif /* _PAM_SESSION_H_ */
