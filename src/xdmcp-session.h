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

#ifndef XDMCP_SESSION_H_
#define XDMCP_SESSION_H_

#include <glib-object.h>
#include <gio/gio.h>

#include "x-authority.h"

G_BEGIN_DECLS

#define XDMCP_SESSION_TYPE (xdmcp_session_get_type())
#define XDMCP_SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XDMCP_SESSION_TYPE, XDMCPSession));

typedef struct
{
    GObject parent_instance;
} XDMCPSession;

typedef struct
{
    GObjectClass parent_class;
} XDMCPSessionClass;

GType xdmcp_session_get_type (void);

XDMCPSession *xdmcp_session_new (guint16 id, GInetAddress *address, guint16 display_number, XAuthority *authority);

guint16 xdmcp_session_get_id (XDMCPSession *session);

GInetAddress *xdmcp_session_get_address (XDMCPSession *session);

guint16 xdmcp_session_get_display_number (XDMCPSession *session);

XAuthority *xdmcp_session_get_authority (XDMCPSession *session);

void xdmcp_session_set_display_class (XDMCPSession *session, const gchar *display_class);

const gchar *xdmcp_session_get_display_class (XDMCPSession *session);

G_END_DECLS

#endif /* XDMCP_SESSION_H_ */
