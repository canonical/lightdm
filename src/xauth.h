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

#ifndef _XAUTH_H_
#define _XAUTH_H_

#include <glib-object.h>
#include <gio/gio.h>

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

GType xauth_get_type (void);

XAuthorization *xauth_new (const gchar *name, const guchar *data, gsize data_length);

XAuthorization *xauth_new_cookie (void);

const gchar *xauth_get_authorization_name (XAuthorization *auth);

const guchar *xauth_get_authorization_data (XAuthorization *auth);

gsize xauth_get_authorization_data_length (XAuthorization *auth);

GFile *xauth_write (XAuthorization *auth, const gchar *path, GError **error);

G_END_DECLS

#endif /* _XAUTH_H_ */
