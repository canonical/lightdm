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

#ifndef _LIGHTDM_SESSION_H_
#define _LIGHTDM_SESSION_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LIGHTDM_TYPE_SESSION            (lightdm_session_get_type())
#define LIGHTDM_SESSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIGHTDM_TYPE_SESSION, LightDMSession));
#define LIGHTDM_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LIGHTDM_TYPE_SESSION, LightDMSessionClass))
#define LIGHTDM_IS_SESSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIGHTDM_TYPE_SESSION))
#define LIGHTDM_IS_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LIGHTDM_TYPE_SESSION))
#define LIGHTDM_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LIGHTDM_TYPE_SESSION, LightDMSessionClass))

typedef struct
{
    GObject parent_instance;
} LightDMSession;

typedef struct
{
    GObjectClass parent_class;
} LightDMSessionClass;

GType lightdm_session_get_type (void);

GList *lightdm_get_sessions (void);

const gchar *lightdm_session_get_key (LightDMSession *session);

const gchar *lightdm_session_get_name (LightDMSession *session);

const gchar *lightdm_session_get_comment (LightDMSession *session);

G_END_DECLS

#endif /* _LIGHTDM_SESSION_H_ */
