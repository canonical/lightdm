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

    /* Reserved */
    void (*reserved1) (void);
    void (*reserved2) (void);
    void (*reserved3) (void);
    void (*reserved4) (void);
    void (*reserved5) (void);
    void (*reserved6) (void);
} LightDMSessionClass;

#ifdef GLIB_VERSION_2_44
typedef LightDMSession *LightDMSession_autoptr;
static inline void glib_autoptr_cleanup_LightDMSession (LightDMSession **_ptr)
{
    glib_autoptr_cleanup_GObject ((GObject **) _ptr);
}
#endif

GType lightdm_session_get_type (void);

GList *lightdm_get_sessions (void);

GList *lightdm_get_remote_sessions (void);

const gchar *lightdm_session_get_key (LightDMSession *session);

const gchar *lightdm_session_get_session_type (LightDMSession *session);

const gchar *lightdm_session_get_name (LightDMSession *session);

const gchar *lightdm_session_get_comment (LightDMSession *session);

G_END_DECLS

#endif /* LIGHTDM_SESSION_H_ */
