/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef LIGHTDM_SESSION_H_
#define LIGHTDM_SESSION_H_

#include <glib-object.h>

G_BEGIN_DECLS

struct _LightDMSessionClass
{
    /*< private >*/
    GObjectClass parent_class;
};

#define LIGHTDM_TYPE_SESSION (lightdm_session_get_type())

G_DECLARE_FINAL_TYPE (LightDMSession, lightdm_session, LIGHTDM, SESSION, GObject)

GList *lightdm_get_sessions (void);

GList *lightdm_get_remote_sessions (void);

const gchar *lightdm_session_get_key (LightDMSession *session);

const gchar *lightdm_session_get_session_type (LightDMSession *session);

const gchar *lightdm_session_get_name (LightDMSession *session);

const gchar *lightdm_session_get_comment (LightDMSession *session);

G_END_DECLS

#endif /* LIGHTDM_SESSION_H_ */
