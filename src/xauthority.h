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

#ifndef XAUTHORITY_H_
#define XAUTHORITY_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define XAUTHORITY_TYPE (xauth_get_type())
#define XAUTHORITY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XAUTHORITY_TYPE, XAuthority));

typedef struct XAuthorityPrivate XAuthorityPrivate;

typedef struct
{
    GObject         parent_instance;
    XAuthorityPrivate *priv;
} XAuthority;

typedef struct
{
    GObjectClass parent_class;
} XAuthorityClass;

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

typedef enum
{
   XAUTH_WRITE_MODE_REPLACE,
   XAUTH_WRITE_MODE_REMOVE,
   XAUTH_WRITE_MODE_SET  
} XAuthWriteMode;

GType xauth_get_type (void);

XAuthority *xauth_new (guint16 family, const guint8 *address, gsize address_length, const gchar *number, const gchar *name, const guint8 *data, gsize data_length);

XAuthority *xauth_new_cookie (guint16 family, const guint8 *address, gsize address_length, const gchar *number);

void xauth_set_family (XAuthority *auth, guint16 family);

guint16 xauth_get_family (XAuthority *auth);

void xauth_set_address (XAuthority *auth, const guint8 *address, gsize address_length);

const guint8 *xauth_get_address (XAuthority *auth);

const gsize xauth_get_address_length (XAuthority *auth);

void xauth_set_number (XAuthority *auth, const gchar *number);

const gchar *xauth_get_number (XAuthority *auth);

void xauth_set_authorization_name (XAuthority *auth, const gchar *name);

const gchar *xauth_get_authorization_name (XAuthority *auth);

void xauth_set_authorization_data (XAuthority *auth, const guint8 *data, gsize data_length);

const guint8 *xauth_get_authorization_data (XAuthority *auth);

guint8 *xauth_copy_authorization_data (XAuthority *auth);

gsize xauth_get_authorization_data_length (XAuthority *auth);

gboolean xauth_write (XAuthority *auth, XAuthWriteMode mode, const gchar *filename, GError **error);

G_END_DECLS

#endif /* XAUTHORITY_H_ */
