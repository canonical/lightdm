/* -*- Mode: C; indent-tabs-mode:nil; tab-width:4 -*-
 *
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include <libxklavier/xklavier.h>

#include "lightdm/layout.h"

/**
 * SECTION:layout
 * @short_description: Control the keyboard layout
 * @include: lightdm.h
 *
 * #LightDMLayout is an object that describes a keyboard that is available on the system.
 */

/**
 * LightDMLayout:
 *
 * #LightDMLayout is an opaque data structure and can only be accessed
 * using the provided functions.
 */

/**
 * LightDMLayoutClass:
 *
 * Class structure for #LightDMLayout.
 */

enum {
    PROP_NAME = 1,
    PROP_SHORT_DESCRIPTION,
    PROP_DESCRIPTION
};

typedef struct
{
    gchar *name;
    gchar *short_description;
    gchar *description;
} LightDMLayoutPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LightDMLayout, lightdm_layout, G_TYPE_OBJECT)

static gboolean have_layouts = FALSE;
static Display *display = NULL;
static XklEngine *xkl_engine = NULL;
static XklConfigRec *xkl_config = NULL;
static GList *layouts = NULL;
static LightDMLayout *default_layout = NULL;

static gchar *
make_layout_string (const gchar *layout, const gchar *variant)
{
    if (!layout || layout[0] == 0)
        return NULL;
    else if (!variant || variant[0] == 0)
        return g_strdup (layout);
    else
        return g_strdup_printf ("%s\t%s", layout, variant);
}

static void
parse_layout_string (const gchar *name, gchar **layout, gchar **variant)
{
    *layout = NULL;
    *variant = NULL;

    if (!name)
        return;

    g_auto(GStrv) split = g_strsplit (name, "\t", 2);
    if (split[0])
    {
        *layout = g_strdup (split[0]);
        if (split[1])
            *variant = g_strdup (split[1]);
    }
}

static void
variant_cb (XklConfigRegistry *config,
           const XklConfigItem *item,
           gpointer data)
{
    g_autofree gchar *full_name = make_layout_string (data, item->name);
    LightDMLayout *layout = g_object_new (LIGHTDM_TYPE_LAYOUT, "name", full_name, "short-description", item->short_description, "description", item->description, NULL);
    layouts = g_list_append (layouts, layout);
}

static void
layout_cb (XklConfigRegistry *config,
           const XklConfigItem *item,
           gpointer data)
{
    LightDMLayout *layout = g_object_new (LIGHTDM_TYPE_LAYOUT, "name", item->name, "short-description", item->short_description, "description", item->description, NULL);
    layouts = g_list_append (layouts, layout);

    xkl_config_registry_foreach_layout_variant (config, item->name, variant_cb, (gpointer) item->name);
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
    if (have_layouts)
        return layouts;

    display = XOpenDisplay (NULL);
    if (display == NULL)
        return NULL;

    xkl_engine = xkl_engine_get_instance (display);
    xkl_config = xkl_config_rec_new ();
    if (!xkl_config_rec_get_from_server (xkl_config, xkl_engine))
        g_warning ("Failed to get Xkl configuration from server");

    XklConfigRegistry *registry = xkl_config_registry_get_instance (xkl_engine);
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
lightdm_set_layout (LightDMLayout *dmlayout)
{
    g_return_if_fail (dmlayout != NULL);
    lightdm_get_layouts();

    g_debug ("Setting keyboard layout to '%s'", lightdm_layout_get_name (dmlayout));

    g_autofree gchar *layout = NULL;
    g_autofree gchar *variant = NULL;
    parse_layout_string (lightdm_layout_get_name (dmlayout), &layout, &variant);

    if (layouts && xkl_config)
    {
        xkl_config->layouts[0] = g_steal_pointer(&layout);
        xkl_config->layouts[1] = NULL;
        xkl_config->variants[0] = g_steal_pointer(&variant);
        xkl_config->variants[1] = NULL;
        default_layout = dmlayout;
    }
    if (!xkl_config_rec_activate (xkl_config, xkl_engine))
        g_warning ("Failed to activate XKL config");
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

    if (layouts && xkl_config && !default_layout)
    {
        g_autofree gchar *full_name = make_layout_string (xkl_config->layouts ? xkl_config->layouts[0] : NULL,
                                                          xkl_config->variants ? xkl_config->variants[0] : NULL);

        for (GList *item = layouts; item; item = item->next)
        {
            LightDMLayout *iter_layout = (LightDMLayout *) item->data;
            if (g_strcmp0 (lightdm_layout_get_name (iter_layout), full_name) == 0)
            {
                default_layout = iter_layout;
                break;
            }
        }
    }

    return default_layout;
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

    LightDMLayoutPrivate *priv = lightdm_layout_get_instance_private (layout);
    return priv->name;
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

    LightDMLayoutPrivate *priv = lightdm_layout_get_instance_private (layout);
    return priv->short_description;
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

    LightDMLayoutPrivate *priv = lightdm_layout_get_instance_private (layout);
    return priv->description;
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
    LightDMLayoutPrivate *priv = lightdm_layout_get_instance_private (self);

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
    LightDMLayout *self = LIGHTDM_LAYOUT (object);

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
lightdm_layout_finalize (GObject *object)
{
    LightDMLayout *self = LIGHTDM_LAYOUT (object);
    LightDMLayoutPrivate *priv = lightdm_layout_get_instance_private (self);

    g_free (priv->name);
    g_free (priv->short_description);
    g_free (priv->description);
}

static void
lightdm_layout_class_init (LightDMLayoutClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lightdm_layout_set_property;
    object_class->get_property = lightdm_layout_get_property;
    object_class->finalize = lightdm_layout_finalize;

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
