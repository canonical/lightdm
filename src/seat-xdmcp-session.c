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

#include "seat-xdmcp-session.h"
#include "xdisplay.h"
#include "xserver-remote.h"

struct SeatXDMCPSessionPrivate
{
    /* The section in the config for this seat */
    gchar *config_section;

    /* Session being serviced */
    XDMCPSession *session;
};

G_DEFINE_TYPE (SeatXDMCPSession, seat_xdmcp_session, SEAT_TYPE);

SeatXDMCPSession *
seat_xdmcp_session_new (const gchar *config_section, XDMCPSession *session)
{
    SeatXDMCPSession *seat;

    seat = g_object_new (SEAT_XDMCP_SESSION_TYPE, NULL);
    seat->priv->session = g_object_ref (session);
    seat_load_config (SEAT (seat), config_section);

    return seat;
}

static Display *
seat_xdmcp_session_add_display (Seat *seat)
{
    XServerRemote *xserver;
    gchar *address;
    XDisplay *display;

    // FIXME: Try IPv6 then fallback to IPv4
    address = g_inet_address_to_string (G_INET_ADDRESS (xdmcp_session_get_address (SEAT_XDMCP_SESSION (seat)->priv->session)));
    xserver = xserver_remote_new (address, xdmcp_session_get_display_number (SEAT_XDMCP_SESSION (seat)->priv->session));

    if (strcmp (xdmcp_session_get_authorization_name (SEAT_XDMCP_SESSION (seat)->priv->session), "") != 0)
    {
        XAuthorization *authorization = NULL;
        gchar *number;

        number = g_strdup_printf ("%d", xdmcp_session_get_display_number (SEAT_XDMCP_SESSION (seat)->priv->session));
        authorization = xauth_new (XAUTH_FAMILY_INTERNET, // FIXME: Handle IPv6
                                   address,
                                   number,
                                   xdmcp_session_get_authorization_name (SEAT_XDMCP_SESSION (seat)->priv->session),
                                   xdmcp_session_get_authorization_data (SEAT_XDMCP_SESSION (seat)->priv->session),
                                   xdmcp_session_get_authorization_data_length (SEAT_XDMCP_SESSION (seat)->priv->session));
        g_free (number);

        xserver_set_authorization (XSERVER (xserver), authorization);
        g_object_unref (authorization);
    }
    g_free (address);

    display = xdisplay_new (SEAT_XDMCP_SESSION (seat)->priv->config_section, XSERVER (xserver));
    g_object_unref (xserver);

    return DISPLAY (display);
}

static void
seat_xdmcp_session_init (SeatXDMCPSession *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_XDMCP_SESSION_TYPE, SeatXDMCPSessionPrivate);
}

static void
seat_xdmcp_session_finalize (GObject *object)
{
    SeatXDMCPSession *self;

    self = SEAT_XDMCP_SESSION (object);

    g_free (self->priv->config_section);

    G_OBJECT_CLASS (seat_xdmcp_session_parent_class)->finalize (object);
}

static void
seat_xdmcp_session_class_init (SeatXDMCPSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SeatClass *seat_class = SEAT_CLASS (klass);

    seat_class->add_display = seat_xdmcp_session_add_display;
    object_class->finalize = seat_xdmcp_session_finalize;

    g_type_class_add_private (klass, sizeof (SeatXDMCPSessionPrivate));
}
