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

#include "xdmcp-session.h"
#include "x-authority.h"

typedef struct
{
    guint16 id;

    GInetAddress *address;

    guint inactive_timeout;

    XAuthority *authority;

    guint16 display_number;

    gchar *display_class;
} XDMCPSessionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (XDMCPSession, xdmcp_session, G_TYPE_OBJECT)

XDMCPSession *
xdmcp_session_new (guint16 id, GInetAddress *address, guint16 display_number, XAuthority *authority)
{
    XDMCPSession *self = g_object_new (XDMCP_SESSION_TYPE, NULL);
    XDMCPSessionPrivate *priv = xdmcp_session_get_instance_private (self);

    priv->id = id;
    priv->address = g_object_ref (address);
    priv->display_number = display_number;
    priv->authority = g_object_ref (authority);

    return self;
}

guint16
xdmcp_session_get_id (XDMCPSession *session)
{
    XDMCPSessionPrivate *priv = xdmcp_session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, 0);
    return priv->id;
}

GInetAddress *
xdmcp_session_get_address (XDMCPSession *session)
{
    XDMCPSessionPrivate *priv = xdmcp_session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->address;
}

guint16
xdmcp_session_get_display_number (XDMCPSession *session)
{
    XDMCPSessionPrivate *priv = xdmcp_session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, 0);
    return priv->display_number;
}

XAuthority *
xdmcp_session_get_authority (XDMCPSession *session)
{
    XDMCPSessionPrivate *priv = xdmcp_session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->authority;
}

void
xdmcp_session_set_display_class (XDMCPSession *session, const gchar *display_class)
{
    XDMCPSessionPrivate *priv = xdmcp_session_get_instance_private (session);
    g_return_if_fail (session != NULL);
    g_free (priv->display_class);
    priv->display_class = g_strdup (display_class);
}

const gchar *
xdmcp_session_get_display_class (XDMCPSession *session)
{
    XDMCPSessionPrivate *priv = xdmcp_session_get_instance_private (session);
    g_return_val_if_fail (session != NULL, NULL);
    return priv->display_class;
}

static void
xdmcp_session_init (XDMCPSession *session)
{
    XDMCPSessionPrivate *priv = xdmcp_session_get_instance_private (session);

    priv->display_class = g_strdup ("");
}

static void
xdmcp_session_finalize (GObject *object)
{
    XDMCPSession *self = XDMCP_SESSION (object);
    XDMCPSessionPrivate *priv = xdmcp_session_get_instance_private (self);

    g_clear_object (&priv->address);
    g_clear_object (&priv->authority);
    g_clear_pointer (&priv->display_class, g_free);

    G_OBJECT_CLASS (xdmcp_session_parent_class)->finalize (object);
}

static void
xdmcp_session_class_init (XDMCPSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = xdmcp_session_finalize;
}
