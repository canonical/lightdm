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

#ifndef _LDM_SESSION_H_
#define _LDM_SESSION_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LDM_TYPE_SESSION            (ldm_session_get_type())
#define LDM_SESSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LDM_TYPE_SESSION, LdmSession));
#define LDM_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LDM_TYPE_SESSION, LdmSessionClass))
#define LDM_IS_SESSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LDM_TYPE_SESSION))
#define LDM_IS_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LDM_TYPE_SESSION))
#define LDM_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LDM_TYPE_SESSION, LdmSessionClass))

typedef struct
{
    GObject parent_instance;
} LdmSession;

typedef struct
{
    GObjectClass parent_class;
} LdmSessionClass;

GType ldm_session_get_type (void);

LdmSession *ldm_session_new (const gchar *key, const gchar *name, const gchar *comment);

const gchar *ldm_session_get_key (LdmSession *session);

const gchar *ldm_session_get_name (LdmSession *session);

const gchar *ldm_session_get_comment (LdmSession *session);

G_END_DECLS

#endif /* _LDM_SESSION_H_ */
