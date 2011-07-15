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

#ifndef _XAUTH_H_
#define _XAUTH_H_

#include <glib-object.h>
#include <gio/gio.h>

#include "user.h"

G_BEGIN_DECLS

#define XAUTH_TYPE (xauth_get_type())
#define XAUTH(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XAUTH_TYPE, XAuthorization));

typedef struct XAuthorizationPrivate XAuthorizationPrivate;

typedef struct
{
    GObject         parent_instance;
    XAuthorizationPrivate *priv;
} XAuthorization;

typedef struct
{
    GObjectClass parent_class;
} XAuthorizationClass;

#define XAUTH_FAMILY_INTERNET 0
#define XAUTH_FAMILY_DECNET 1
#define XAUTH_FAMILY_CHAOS 2
#define XAUTH_FAMILY_SERVER_INTERPRETED 5
#define XAUTH_FAMILY_INTERNET6 6
#define XAUTH_FAMILY_LOCALHOST 252
#define XAUTH_FAMILY_KRB5_PRINCIPAL 253
#define XAUTH_FAMILY_NETNAME 254
#define XAUTH_FAMILY_LOCAL 256
#define XAUTH_FAMILY_WILD 65535

GType xauth_get_type (void);

XAuthorization *xauth_new (guint16 family, const gchar *address, const gchar *number, const gchar *name, const guint8 *data, gsize data_length);

XAuthorization *xauth_new_cookie (guint16 family, const gchar *address, const gchar *number);

void xauth_set_family (XAuthorization *auth, guint16 family);

guint16 xauth_get_family (XAuthorization *auth);

void xauth_set_address (XAuthorization *auth, const gchar *address);

const gchar *xauth_get_address (XAuthorization *auth);

void xauth_set_number (XAuthorization *auth, const gchar *number);

const gchar *xauth_get_number (XAuthorization *auth);

void xauth_set_authorization_name (XAuthorization *auth, const gchar *name);

const gchar *xauth_get_authorization_name (XAuthorization *auth);

void xauth_set_authorization_data (XAuthorization *auth, const guint8 *data, gsize data_length);

const guint8 *xauth_get_authorization_data (XAuthorization *auth);

guint8 *xauth_copy_authorization_data (XAuthorization *auth);

gsize xauth_get_authorization_data_length (XAuthorization *auth);

gboolean xauth_update (XAuthorization *auth, User *user, GFile *file, GError **error);

gboolean xauth_remove (XAuthorization *auth, User *user, GFile *file, GError **error);

G_END_DECLS

#endif /* _XAUTH_H_ */
