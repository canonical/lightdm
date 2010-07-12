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

#include <stdlib.h>
#include <dbus/dbus-glib.h>

#include "display-manager.h"
#include "display-manager-glue.h"

enum {
    PROP_0,
    PROP_CONFIG,
};

enum {
    DISPLAY_ADDED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct DisplayManagerPrivate
{
    GKeyFile *config;

    DBusGConnection *connection;

    GList *displays;
};

G_DEFINE_TYPE (DisplayManager, display_manager, G_TYPE_OBJECT);

DisplayManager *
display_manager_new (GKeyFile *config)
{
    return g_object_new (DISPLAY_MANAGER_TYPE, "config", config, NULL);
}

static gboolean
index_used (DisplayManager *manager, gint index)
{
    GList *link;

    for (link = manager->priv->displays; link; link = link->next)
    {
        Display *display = link->data;
        if (display_get_index (display) == index)
            return TRUE;
    }

    return FALSE;
}

static gint
get_free_index (DisplayManager *manager)
{
    gint min_index = 0, index;
    gchar *numbers;

    numbers = g_key_file_get_value (manager->priv->config, "LightDM", "display_numbers", NULL);
    if (numbers)
    {
        min_index = atoi (numbers);
        g_free (numbers);
    }

    for (index = min_index; index_used (manager, index); index++);

    return index;
}

Display *
display_manager_add_display (DisplayManager *manager)
{
    Display *display;

    display = display_new (manager->priv->config, get_free_index (manager));

    g_signal_emit (manager, signals[DISPLAY_ADDED], 0, display);

    return display;
}

gboolean
display_manager_switch_to_user (Display *display, char *username, GError *error)
{
    return TRUE;
}

static void
display_manager_init (DisplayManager *manager)
{
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, DISPLAY_MANAGER_TYPE, DisplayManagerPrivate);
}

static void
display_manager_set_property(GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    DisplayManager *self;

    self = DISPLAY_MANAGER (object);

    switch (prop_id) {
    case PROP_CONFIG:
        self->priv->config = g_value_get_pointer (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}


static void
display_manager_get_property(GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    DisplayManager *self;

    self = DISPLAY_MANAGER (object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_pointer (value, self->priv->config);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
display_manager_class_init (DisplayManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = display_manager_set_property;
    object_class->get_property = display_manager_get_property;

    g_type_class_add_private (klass, sizeof (DisplayManagerPrivate));

    g_object_class_install_property (object_class,
                                     PROP_CONFIG,
                                     g_param_spec_pointer ("config",
                                                           "config",
                                                           "Configuration",
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  
    signals[DISPLAY_ADDED] =
        g_signal_new ("display-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, display_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, DISPLAY_TYPE);

    dbus_g_object_type_install_info (DISPLAY_MANAGER_TYPE, &dbus_glib_display_manager_object_info);
}
