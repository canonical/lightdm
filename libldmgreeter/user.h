/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#ifndef _LDM_USER_H_
#define _LDM_USER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LDM_TYPE_USER            (ldm_user_get_type())
#define LDM_USER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LDM_TYPE_USER, LdmUser));
#define LDM_USER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LDM_TYPE_USER, LdmUserClass))
#define LDM_IS_USER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LDM_TYPE_USER))
#define LDM_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LDM_TYPE_USER))
#define LDM_USER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LDM_TYPE_USER, LdmUserClass))

typedef struct _LdmUser        LdmUser;
typedef struct _LdmUserClass   LdmUserClass;
typedef struct _LdmUserPrivate LdmUserPrivate;

struct _LdmUser
{
    GObject         parent_instance;
    LdmUserPrivate *priv;
};

struct _LdmUserClass
{
    GObjectClass parent_class;
};

GType ldm_user_get_type (void);

LdmUser *ldm_user_new (const gchar *name, const gchar *real_name, const gchar *image, gboolean logged_in);

const gchar *ldm_user_get_name (LdmUser *user);

const gchar *ldm_user_get_real_name (LdmUser *user);

const gchar *ldm_user_get_display_name (LdmUser *user);

const gchar *ldm_user_get_image (LdmUser *user);

gboolean ldm_user_get_logged_in (LdmUser *user);

G_END_DECLS

#endif /* _LDM_USER_H_ */
