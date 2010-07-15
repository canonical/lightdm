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
#include "xdmcp-server.h"
#include "xserver.h"
#include "theme.h"

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

    GList *displays;
  
    XDMCPServer *xdmcp_server;
};

G_DEFINE_TYPE (DisplayManager, display_manager, G_TYPE_OBJECT);

DisplayManager *
display_manager_new (GKeyFile *config)
{
    return g_object_new (DISPLAY_MANAGER_TYPE, "config", config, NULL);
}

static gboolean
display_number_used (DisplayManager *manager, guint display_number)
{
    GList *link;

    for (link = manager->priv->displays; link; link = link->next)
    {
        Display *display = link->data;      
        XServer *xserver = display_get_xserver (display);
        if (xserver && xserver_get_hostname (xserver) == NULL && xserver_get_display_number (xserver) == display_number)
            return TRUE;
    }

    return FALSE;
}

static guint
get_free_display_number (DisplayManager *manager)
{
    guint display_number = 0;

    while (display_number_used (manager, display_number))
        display_number++;
  
    return display_number;
}

static Display *
add_display (DisplayManager *manager)
{
    Display *display;

    display = display_new (manager->priv->config, g_list_length (manager->priv->displays));
    manager->priv->displays = g_list_append (manager->priv->displays, display);

    g_signal_emit (manager, signals[DISPLAY_ADDED], 0, display);

    return display;
}

gboolean
display_manager_switch_to_user (DisplayManager *manager, char *username, GError *error)
{
    return TRUE;
}

static void
xdmcp_session_cb (XDMCPServer *server, XDMCPSession *session, DisplayManager *manager)
{
    Display *display;
    gchar *address;

    display = add_display (manager);
    address = g_inet_address_to_string (G_INET_ADDRESS (xdmcp_session_get_address (session)));
    display_start (display, address, xdmcp_session_get_display_number (session), NULL, 0);
    g_free (address);
}

void
display_manager_start (DisplayManager *manager)
{
    gchar *displays;
    gchar **tokens, **i;
  
    /* Start the first display */
    displays = g_key_file_get_string (manager->priv->config, "LightDM", "displays", NULL);
    if (!displays)
        displays = g_strdup ("");
    tokens = g_strsplit (displays, " ", -1);
    g_free (displays);

    for (i = tokens; *i; i++)
    {
        Display *display;
        gchar *default_user;
        gint user_timeout;
        gchar *display_name;
        guint display_number;

        display_name = *i;
        g_debug ("Loading display %s", display_name);

        display = add_display (manager);

        /* Automatically log in or start a greeter session */  
        default_user = g_key_file_get_value (manager->priv->config, "Default User", "name", NULL);
        //FIXME default_user_session = g_key_file_get_value (manager->priv->config, "Default User", "session", NULL); // FIXME
        user_timeout = g_key_file_get_integer (manager->priv->config, "Default User", "timeout", NULL);
        if (user_timeout < 0)
            user_timeout = 0;

        if (default_user)
        {
            if (user_timeout == 0)
                g_debug ("Starting session for user %s", default_user);
            else
                g_debug ("Starting session for user %s in %d seconds", default_user, user_timeout);
        }

        if (g_key_file_has_key (manager->priv->config, display_name, "display-number", NULL))
            display_number = g_key_file_get_integer (manager->priv->config, display_name, "display-number", NULL);
        else
            display_number = get_free_display_number (manager);

        display_start (display, NULL, display_number, default_user, user_timeout);
        g_free (default_user);
    }
    g_strfreev (tokens);

    if (g_key_file_get_boolean (manager->priv->config, "xdmcp", "enabled", NULL))
    {
        gchar *key;

        manager->priv->xdmcp_server = xdmcp_server_new (manager->priv->config);
        g_signal_connect (manager->priv->xdmcp_server, "session-added", G_CALLBACK (xdmcp_session_cb), manager);

        key = g_key_file_get_string (manager->priv->config, "xdmcp", "key", NULL);
        if (key)
            xdmcp_server_set_authentication_key (manager->priv->xdmcp_server, key);
        g_free (key);

        xdmcp_server_start (manager->priv->xdmcp_server); 
    }
}

static void
display_manager_init (DisplayManager *manager)
{
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, DISPLAY_MANAGER_TYPE, DisplayManagerPrivate);
}

static void
display_manager_set_property (GObject      *object,
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
display_manager_get_property (GObject    *object,
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
display_manager_finalize (GObject *object)
{
    DisplayManager *self;
    GList *link;

    self = DISPLAY_MANAGER (object);

    if (self->priv->xdmcp_server)
        g_object_unref (self->priv->xdmcp_server);
    for (link = self->priv->displays; link; link = link->next)
        g_object_unref (link->data);
}

static void
display_manager_class_init (DisplayManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = display_manager_set_property;
    object_class->get_property = display_manager_get_property;
    object_class->finalize = display_manager_finalize;

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
