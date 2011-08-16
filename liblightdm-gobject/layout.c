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

#include <libxklavier/xklavier.h>

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
} LightDMLayoutPrivate;

G_DEFINE_TYPE (LightDMLayout, lightdm_layout, G_TYPE_OBJECT);

#define GET_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_LAYOUT, LightDMLayoutPrivate)

static gboolean have_layouts = FALSE;
static Display *display = NULL;
static XklEngine *xkl_engine = NULL;
static XklConfigRec *xkl_config = NULL;
static GList *layouts = NULL;

static void
layout_cb (XklConfigRegistry *config,
           const XklConfigItem *item,
           gpointer data)
{
    LightDMLayout *layout;

    layout = g_object_new (LIGHTDM_TYPE_LAYOUT, "name", item->name, "short-description", item->short_description, "description", item->description, NULL);
    layouts = g_list_append (layouts, layout);
}

/**
 * lightdm_get_layouts:
 *
 * Get a list of keyboard layouts to present to the user.
 *
 * Return value: (element-type LightDMLayout) (transfer none): A list of #LightDMLayout that should be presented to the user.
 **/
GList *
lightdm_get_layouts (void)
{
    XklConfigRegistry *registry;

    if (have_layouts)
        return layouts;

    display = XOpenDisplay (NULL);
    xkl_engine = xkl_engine_get_instance (display);
    xkl_config = xkl_config_rec_new ();
    if (!xkl_config_rec_get_from_server (xkl_config, xkl_engine))
        g_warning ("Failed to get Xkl configuration from server");

    registry = xkl_config_registry_get_instance (xkl_engine);
    xkl_config_registry_load (registry, FALSE);
    xkl_config_registry_foreach_layout (registry, layout_cb, NULL);
    g_object_unref (registry);

    have_layouts = TRUE;

    return layouts;
}

/**
 * lightdm_set_layout:
 * @layout: The layout to use
 *
 * Set the layout for this session.
 **/
void
lightdm_set_layout (LightDMLayout *layout)
{
    XklConfigRec *config;

    g_return_if_fail (layout != NULL);

    g_debug ("Setting keyboard layout to %s", lightdm_layout_get_name (layout));

    config = xkl_config_rec_new ();
    config->layouts = g_malloc (sizeof (gchar *) * 2);
    config->model = g_strdup (xkl_config->model);
    config->layouts[0] = g_strdup (lightdm_layout_get_name (layout));
    config->layouts[1] = NULL;
    if (!xkl_config_rec_activate (config, xkl_engine))
        g_warning ("Failed to activate XKL config");
    g_object_unref (config);
}

/**
 * lightdm_get_layout:
 *
 * Get the current keyboard layout.
 *
 * Return value: (transfer none): The currently active layout for this user.
 **/
LightDMLayout *
lightdm_get_layout (void)
{
    lightdm_get_layouts ();
    if (layouts)
        return (LightDMLayout *) g_list_first (layouts);
    else
        return NULL;
}

/**
 * lightdm_layout_get_name:
 * @layout: A #LightDMLayout
 * 
 * Get the name of a layout.
 * 
 * Return value: The name of the layout
 **/
const gchar *
lightdm_layout_get_name (LightDMLayout *layout)
{
    g_return_val_if_fail (LIGHTDM_IS_LAYOUT (layout), NULL);
    return GET_PRIVATE (layout)->name;
}

/**
 * lightdm_layout_get_short_description:
 * @layout: A #LightDMLayout
 * 
 * Get the short description of a layout.
 *
 * Return value: A short description of the layout
 **/
const gchar *
lightdm_layout_get_short_description (LightDMLayout *layout)
{
    g_return_val_if_fail (LIGHTDM_IS_LAYOUT (layout), NULL);
    return GET_PRIVATE (layout)->short_description;
}

/**
 * lightdm_layout_get_description:
 * @layout: A #LightDMLayout
 * 
 * Get the long description of a layout.
 * 
 * Return value: A long description of the layout
 **/
const gchar *
lightdm_layout_get_description (LightDMLayout *layout)
{
    g_return_val_if_fail (LIGHTDM_IS_LAYOUT (layout), NULL);
    return GET_PRIVATE (layout)->description;
}

static void
lightdm_layout_init (LightDMLayout *layout)
{
}

static void
lightdm_layout_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    LightDMLayout *self = LIGHTDM_LAYOUT (object);
    LightDMLayoutPrivate *priv = GET_PRIVATE (self);

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
lightdm_layout_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    LightDMLayout *self;

    self = LIGHTDM_LAYOUT (object);

    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string (value, lightdm_layout_get_name (self));
        break;
    case PROP_SHORT_DESCRIPTION:
        g_value_set_string (value, lightdm_layout_get_short_description (self));
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, lightdm_layout_get_description (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_layout_class_init (LightDMLayoutClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
    g_type_class_add_private (klass, sizeof (LightDMLayoutPrivate));

    object_class->set_property = lightdm_layout_set_property;
    object_class->get_property = lightdm_layout_get_property;

    g_object_class_install_property (object_class,
                                     PROP_NAME,
                                     g_param_spec_string ("name",
                                                          "name",
                                                          "Name of the layout",
                                                          NULL,
                                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_SHORT_DESCRIPTION,
                                     g_param_spec_string ("short-description",
                                                          "short-description",
                                                          "Short description of the layout",
                                                          NULL,
                                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_DESCRIPTION,
                                     g_param_spec_string ("description",
                                                          "description",
                                                          "Long description of the layout",
                                                          NULL,
                                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
