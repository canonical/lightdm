/*
 * Copyright (C) 2010 Canonical Ltd.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef _PAM_AUTHENTICATOR_H_
#define _PAM_AUTHENTICATOR_H_

#include <glib-object.h>
#include <security/pam_appl.h>

G_BEGIN_DECLS

#define PAM_AUTHENTICATOR_TYPE (pam_authenticator_get_type())
#define PAM_AUTHENTICATOR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PAM_AUTHENTICATOR_TYPE, PAMAuthenticator));

typedef struct PAMAuthenticatorPrivate PAMAuthenticatorPrivate;

typedef struct
{
    GObject         parent_instance;
    PAMAuthenticatorPrivate *priv;
} PAMAuthenticator;

typedef struct
{
    GObjectClass parent_class;
  
    void (*got_messages)(PAMAuthenticator *pam_authenticator, int num_msg, const struct pam_message **msg);
    void (*authentication_complete)(PAMAuthenticator *pam_authenticator, int result);
} PAMAuthenticatorClass;

GType pam_authenticator_get_type (void);

PAMAuthenticator *pam_authenticator_new (void);

gboolean pam_authenticator_start (PAMAuthenticator *authenticator, const char *username, GError **error);

const struct pam_message **pam_authenticator_get_messages (PAMAuthenticator *authenticator);

int  pam_authenticator_get_num_messages (PAMAuthenticator *authenticator);

void pam_authenticator_cancel (PAMAuthenticator *authenticator);

void pam_authenticator_respond (PAMAuthenticator *authenticator, struct pam_response *response);

G_END_DECLS

#endif /* _PAM_AUTHENTICATOR_H_ */
