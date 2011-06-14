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
#include "xdmcp-session-private.h"

G_DEFINE_TYPE (XDMCPSession, xdmcp_session, G_TYPE_OBJECT);

XDMCPSession *
xdmcp_session_new (guint16 id)
{
    XDMCPSession *self = g_object_new (XDMCP_SESSION_TYPE, NULL);

    self->priv->id = id;

    return self;
}

guint16
xdmcp_session_get_id (XDMCPSession *session)
{
    g_return_val_if_fail (session != NULL, 0);
    return session->priv->id;
}

const gchar *
xdmcp_session_get_manufacturer_display_id (XDMCPSession *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->manufacturer_display_id;
}

const GInetAddress *
xdmcp_session_get_address (XDMCPSession *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->address;
}

const gchar *
xdmcp_session_get_authorization_name (XDMCPSession *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->authorization_name;
}

const guchar *
xdmcp_session_get_authorization_data (XDMCPSession *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->authorization_data;
}

const gsize
xdmcp_session_get_authorization_data_length (XDMCPSession *session)
{
    g_return_val_if_fail (session != NULL, 0);
    return session->priv->authorization_data_length;
}

guint16
xdmcp_session_get_display_number (XDMCPSession *session)
{
    g_return_val_if_fail (session != NULL, 0);
    return session->priv->display_number;
}

const gchar *
xdmcp_session_get_display_class (XDMCPSession *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->display_class;
}

static void
xdmcp_session_init (XDMCPSession *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, XDMCP_SESSION_TYPE, XDMCPSessionPrivate);
    session->priv->manufacturer_display_id = g_strdup ("");
    session->priv->authorization_name = g_strdup ("");
    session->priv->display_class = g_strdup ("");
}

static void
xdmcp_session_finalize (GObject *object)
{
    XDMCPSession *self;

    self = XDMCP_SESSION (object);
  
    g_free (self->priv->manufacturer_display_id);
    if (self->priv->address)
        g_object_unref (self->priv->address);
    if (self->priv->address6)
        g_object_unref (self->priv->address6);
    g_free (self->priv->authorization_name);
    g_free (self->priv->display_class);

    G_OBJECT_CLASS (xdmcp_session_parent_class)->finalize (object);
}

static void
xdmcp_session_class_init (XDMCPSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = xdmcp_session_finalize;  

    g_type_class_add_private (klass, sizeof (XDMCPSessionPrivate));
}
