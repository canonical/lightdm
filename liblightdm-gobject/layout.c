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

#include "lightdm/layout.h"

enum {
    PROP_0,
    PROP_NAME,
    PROP_SHORT_DESCRIPTION,
    PROP_DESCRIPTION
};

typedef struct
{
    gchar *name;
    gchar *short_description;
    gchar *description;
} LdmLayoutPrivate;

G_DEFINE_TYPE (LdmLayout, ldm_layout, G_TYPE_OBJECT);

#define GET_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LDM_TYPE_LAYOUT, LdmLayoutPrivate)

/**
 * ldm_layout_new:
 * 
 * Create a new layout.
 * @name: The layout name
 * @short_description: Short description for the layout
 * @description: Long description for the layout
 * 
 * Return value: the new #LdmLayout
 **/
LdmLayout *
ldm_layout_new (const gchar *name, const gchar *short_description, const gchar *description)
{
    return g_object_new (LDM_TYPE_LAYOUT, "name", name, "short-description", short_description, "description", description, NULL);
}

/**
 * ldm_layout_get_name:
 * @layout: A #LdmLayout
 * 
 * Get the name of a layout.
 * 
 * Return value: The name of the layout
 **/
const gchar *
ldm_layout_get_name (LdmLayout *layout)
{
    g_return_val_if_fail (LDM_IS_LAYOUT (layout), NULL);
    return GET_PRIVATE (layout)->name;
}

/**
 * ldm_layout_get_short_description:
 * @layout: A #LdmLayout
 * 
 * Get the short description of a layout.
 *
 * Return value: A short description of the layout
 **/
const gchar *
ldm_layout_get_short_description (LdmLayout *layout)
{
    g_return_val_if_fail (LDM_IS_LAYOUT (layout), NULL);
    return GET_PRIVATE (layout)->short_description;
}

/**
 * ldm_layout_get_description:
 * @layout: A #LdmLayout
 * 
 * Get the long description of a layout.
 * 
 * Return value: A long description of the layout
 **/
const gchar *
ldm_layout_get_description (LdmLayout *layout)
{
    g_return_val_if_fail (LDM_IS_LAYOUT (layout), NULL);
    return GET_PRIVATE (layout)->description;
}

static void
ldm_layout_init (LdmLayout *layout)
{
}

static void
ldm_layout_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    LdmLayout *self = LDM_LAYOUT (object);
    LdmLayoutPrivate *priv = GET_PRIVATE (self);

    switch (prop_id) {
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_strdup (g_value_get_string (value));
        break;
    case PROP_SHORT_DESCRIPTION:
        g_free (priv->short_description);
        priv->short_description = g_strdup (g_value_get_string (value));
        break;
    case PROP_DESCRIPTION:
        g_free (priv->description);
        priv->description = g_strdup (g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ldm_layout_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    LdmLayout *self;

    self = LDM_LAYOUT (object);

    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string (value, ldm_layout_get_name (self));
        break;
    case PROP_SHORT_DESCRIPTION:
        g_value_set_string (value, ldm_layout_get_short_description (self));
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, ldm_layout_get_description (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ldm_layout_class_init (LdmLayoutClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
    g_type_class_add_private (klass, sizeof (LdmLayoutPrivate));

    object_class->set_property = ldm_layout_set_property;
    object_class->get_property = ldm_layout_get_property;

    g_object_class_install_property(object_class,
                                    PROP_NAME,
                                    g_param_spec_string("name",
                                                        "name",
                                                        "Name of the layout",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_SHORT_DESCRIPTION,
                                    g_param_spec_string("short-description",
                                                        "short-description",
                                                        "Short description of the layout",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_DESCRIPTION,
                                    g_param_spec_string("description",
                                                        "description",
                                                        "Long description of the layout",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
