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

#include "lightdm/session.h"

enum {
    PROP_0,
    PROP_KEY,
    PROP_NAME,
    PROP_COMMENT
};

typedef struct
{
    gchar *key;
    gchar *name;
    gchar *comment;
} LightDMSessionPrivate;

G_DEFINE_TYPE (LightDMSession, lightdm_session, G_TYPE_OBJECT);

#define GET_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_SESSION, LightDMSessionPrivate)

/**
 * lightdm_session_new:
 * 
 * Create a new session.
 * @key: The unique key for this session
 * @name: The name of this session
 * @comment: The comment for this session
 * 
 * Return value: the new #LightDMSession
 **/
LightDMSession *
lightdm_session_new (const gchar *key, const gchar *name, const gchar *comment)
{
    return g_object_new (LIGHTDM_TYPE_SESSION, "key", key, "name", name, "comment", comment, NULL);
}

/**
 * lightdm_session_get_key
 * @session: A #LightDMSession
 * 
 * Get the key for a session
 * 
 * Return value: The session key
 **/
const gchar *
lightdm_session_get_key (LightDMSession *session)
{
    g_return_val_if_fail (LIGHTDM_IS_SESSION (session), NULL);
    return GET_PRIVATE (session)->key;
}

/**
 * lightdm_session_get_name
 * @session: A #LightDMSession
 * 
 * Get the name for a session
 * 
 * Return value: The session name
 **/
const gchar *
lightdm_session_get_name (LightDMSession *session)
{
    g_return_val_if_fail (LIGHTDM_IS_SESSION (session), NULL);
    return GET_PRIVATE (session)->name;
}

/**
 * lightdm_session_get_comment
 * @session: A #LightDMSession
 * 
 * Get the comment for a session
 * 
 * Return value: The session comment
 **/
const gchar *
lightdm_session_get_comment (LightDMSession *session)
{
    g_return_val_if_fail (LIGHTDM_IS_SESSION (session), NULL);
    return GET_PRIVATE (session)->comment;
}

static void
lightdm_session_init (LightDMSession *session)
{
}

static void
lightdm_session_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LightDMSession *self = LIGHTDM_SESSION (object);
    LightDMSessionPrivate *priv = GET_PRIVATE (self);

    switch (prop_id) {
    case PROP_KEY:
        g_free (priv->key);
        priv->key = g_strdup (g_value_get_string (value));
        break;
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_strdup (g_value_get_string (value));
        break;
    case PROP_COMMENT:
        g_free (priv->comment);
        priv->comment = g_strdup (g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_session_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LightDMSession *self;

    self = LIGHTDM_SESSION (object);

    switch (prop_id) {
    case PROP_KEY:
        g_value_set_string (value, lightdm_session_get_key (self));
        break;
    case PROP_NAME:
        g_value_set_string (value, lightdm_session_get_name (self));
        break;
    case PROP_COMMENT:
        g_value_set_string (value, lightdm_session_get_comment (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_session_class_init (LightDMSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
    g_type_class_add_private (klass, sizeof (LightDMSessionPrivate));

    object_class->set_property = lightdm_session_set_property;
    object_class->get_property = lightdm_session_get_property;

    g_object_class_install_property(object_class,
                                    PROP_KEY,
                                    g_param_spec_string("key",
                                                        "key",
                                                        "Session key",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_NAME,
                                    g_param_spec_string("name",
                                                        "name",
                                                        "Session name",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_COMMENT,
                                    g_param_spec_string("comment",
                                                        "comment",
                                                        "Session comment",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
