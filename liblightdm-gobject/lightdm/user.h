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

#ifndef _LIGHTDM_USER_H_
#define _LIGHTDM_USER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LIGHTDM_TYPE_USER            (lightdm_user_get_type())
#define LIGHTDM_USER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIGHTDM_TYPE_USER, LightDMUser));
#define LIGHTDM_USER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LIGHTDM_TYPE_USER, LightDMUserClass))
#define LIGHTDM_IS_USER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIGHTDM_TYPE_USER))
#define LIGHTDM_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LIGHTDM_TYPE_USER))
#define LIGHTDM_USER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LIGHTDM_TYPE_USER, LightDMUserClass))

typedef struct
{
    GObject parent_instance;
} LightDMUser;

typedef struct
{
    GObjectClass parent_class;
} LightDMUserClass;

GType lightdm_user_get_type (void);

const gchar *lightdm_user_get_name (LightDMUser *user);

const gchar *lightdm_user_get_real_name (LightDMUser *user);

const gchar *lightdm_user_get_display_name (LightDMUser *user);

const gchar *lightdm_user_get_home_directory (LightDMUser *user);

const gchar *lightdm_user_get_image (LightDMUser *user);

const gchar *lightdm_user_get_language (LightDMUser *user);

const gchar *lightdm_user_get_layout (LightDMUser *user);

const gchar *lightdm_user_get_session (LightDMUser *user);

gboolean lightdm_user_get_logged_in (LightDMUser *user);

G_END_DECLS

#endif /* _LIGHTDM_USER_H_ */
