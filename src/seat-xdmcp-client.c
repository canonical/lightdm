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

#include <string.h>

#include "seat-xdmcp-client.h"
#include "xdisplay.h"
#include "configuration.h"
#include "xserver-local.h"

struct SeatXDMCPClientPrivate
{
    /* The section in the config for this seat */  
    gchar *config_section;

    /* The display we are running */
    XDisplay *display;
};

G_DEFINE_TYPE (SeatXDMCPClient, seat_xdmcp_client, SEAT_TYPE);

SeatXDMCPClient *
seat_xdmcp_client_new (const gchar *config_section)
{
    SeatXDMCPClient *seat;

    seat = g_object_new (SEAT_XDMCP_CLIENT_TYPE, NULL);
    seat->priv->config_section = g_strdup (config_section);
    seat_load_config (SEAT (seat), config_section);

    return seat;
}

static Display *
seat_xdmcp_client_add_display (Seat *seat)
{
    XServerLocal *xserver;
    XAuthority *authority = NULL;
    gchar *xdmcp_manager;
    gint port;
    //gchar *key;
  
    g_assert (SEAT_XDMCP_CLIENT (seat)->priv->display == NULL);

    g_debug ("Starting seat %s", SEAT_XDMCP_CLIENT (seat)->priv->config_section);

    xserver = xserver_local_new (SEAT_XDMCP_CLIENT (seat)->priv->config_section);

    xdmcp_manager = config_get_string (config_get_instance (), "SeatDefaults", "xdmcp-manager");
    if (!xdmcp_manager)
        xdmcp_manager = config_get_string (config_get_instance (), SEAT_XDMCP_CLIENT (seat)->priv->config_section, "xdmcp-manager");
    xserver_local_set_xdmcp_server (xserver, xdmcp_manager);
    g_free (xdmcp_manager);

    if (config_has_key (config_get_instance (), "SeatDefaults", "xdmcp-port"))
        port = config_get_integer (config_get_instance (), "SeatDefaults", "xdmcp-port");
    else
        port = config_get_integer (config_get_instance (), SEAT_XDMCP_CLIENT (seat)->priv->config_section, "xdmcp-port");
    if (port > 0)
        xserver_local_set_xdmcp_port (xserver, port);
    /*FIXME key = config_get_string (config_get_instance (), SEAT_XDMCP_CLIENT (seat)->priv->config_section, "key");
    if (key)
    {
        guchar data[8];

        string_to_xdm_auth_key (key, data);
        xserver_set_authentication (xserver, "XDM-AUTHENTICATION-1", data, 8);
        authority = xauth_new (XAUTH_FAMILY_WILD, "", "", "XDM-AUTHORIZATION-1", data, 8);
    }*/

    xserver_set_authority (XSERVER (xserver), authority);
    g_object_unref (authority);

    SEAT_XDMCP_CLIENT (seat)->priv->display = g_object_ref (xdisplay_new (SEAT_XDMCP_CLIENT (seat)->priv->config_section, XSERVER (xserver)));
    g_object_unref (xserver);

    return DISPLAY (SEAT_XDMCP_CLIENT (seat)->priv->display);
}

static void
seat_xdmcp_client_init (SeatXDMCPClient *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_XDMCP_CLIENT_TYPE, SeatXDMCPClientPrivate);
}

static void
seat_xdmcp_client_finalize (GObject *object)
{
    SeatXDMCPClient *self;

    self = SEAT_XDMCP_CLIENT (object);

    g_free (self->priv->config_section);

    G_OBJECT_CLASS (seat_xdmcp_client_parent_class)->finalize (object);
}

static void
seat_xdmcp_client_class_init (SeatXDMCPClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SeatClass *seat_class = SEAT_CLASS (klass);

    seat_class->add_display = seat_xdmcp_client_add_display;
    object_class->finalize = seat_xdmcp_client_finalize;

    g_type_class_add_private (klass, sizeof (SeatXDMCPClientPrivate));
}
