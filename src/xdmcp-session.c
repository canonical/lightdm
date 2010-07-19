/*
 * Copyright (C) 2010 Robert Ancell.
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

enum {
    PROP_0,
    PROP_ID,
    PROP_MANUFACTURER_DISPLAY_ID
};

G_DEFINE_TYPE (XDMCPSession, xdmcp_session, G_TYPE_OBJECT);

XDMCPSession *
xdmcp_session_new (guint16 id)
{
    return g_object_new (XDMCP_SESSION_TYPE, "id", id, NULL);
}

guint16
xdmcp_session_get_id (XDMCPSession *session)
{
    return session->priv->id;
}

const gchar *
xdmcp_session_get_manufacturer_display_id (XDMCPSession *session)
{
    return session->priv->manufacturer_display_id;
}

const GInetAddress *
xdmcp_session_get_address (XDMCPSession *session)
{
    return session->priv->address;
}

const gchar *
xdmcp_session_get_authorization_name (XDMCPSession *session)
{
    return session->priv->authorization_name;
}

const guchar *
xdmcp_session_get_authorization_data (XDMCPSession *session)
{
    return session->priv->authorization_data;  
}

const gsize
xdmcp_session_get_authorization_data_length (XDMCPSession *session)
{
    return session->priv->authorization_data_length;
}

guint16
xdmcp_session_get_display_number (XDMCPSession *session)
{
    return session->priv->display_number;
}

const gchar *
xdmcp_session_get_display_class (XDMCPSession *session)
{
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
xdmcp_session_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    XDMCPSession *self;

    self = XDMCP_SESSION (object);

    switch (prop_id) {
    case PROP_ID:
        self->priv->id = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}


static void
xdmcp_session_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    XDMCPSession *self;

    self = XDMCP_SESSION (object);

    switch (prop_id) {
    case PROP_ID:
        g_value_set_int (value, self->priv->id);
        break;
    case PROP_MANUFACTURER_DISPLAY_ID:
        g_value_set_string (value, self->priv->manufacturer_display_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
xdmcp_session_finalize (GObject *object)
{
    XDMCPSession *self;

    self = XDMCP_SESSION (object);
  
    g_free (self->priv->manufacturer_display_id);
    if (self->priv->address)
        g_object_unref (self->priv->address);
    g_free (self->priv->authorization_name);
    g_free (self->priv->display_class);
}

static void
xdmcp_session_class_init (XDMCPSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = xdmcp_session_set_property;
    object_class->get_property = xdmcp_session_get_property;
    object_class->finalize = xdmcp_session_finalize;  

    g_object_class_install_property (object_class,
                                     PROP_ID,
                                     g_param_spec_int ("id",
                                                       "id",
                                                       "Session ID",
                                                       0, G_MAXUINT16, 0,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_MANUFACTURER_DISPLAY_ID,
                                     g_param_spec_string ("manufacturer-display-id",
                                                          "manufacturer-display-id",
                                                          "Manufacturer Display ID",
                                                          NULL,
                                                          G_PARAM_READABLE));

    g_type_class_add_private (klass, sizeof (XDMCPSessionPrivate));
}
