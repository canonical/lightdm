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

#ifndef _PAM_SESSION_H_
#define _PAM_SESSION_H_

#include <glib-object.h>
#include <security/pam_appl.h>

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
    void (*ended)(PAMSession *pam_session);
} PAMSessionClass;

GType pam_session_get_type (void);

PAMSession *pam_session_new (const gchar *service, const gchar *username);

gboolean pam_session_get_in_session (PAMSession *session);

void pam_session_authorize (PAMSession *session);

gboolean pam_session_start (PAMSession *session, GError **error);

const gchar *pam_session_strerror (PAMSession *session, int error);

const gchar *pam_session_get_username (PAMSession *session);

const struct pam_message **pam_session_get_messages (PAMSession *session);

gint pam_session_get_num_messages (PAMSession *session);

void pam_session_respond (PAMSession *session, struct pam_response *response);

// FIXME: Do in unref
void pam_session_end (PAMSession *session);

G_END_DECLS

#endif /* _PAM_SESSION_H_ */
