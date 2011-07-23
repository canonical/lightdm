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
    /* Remote display */
    XDisplay *display;
  
    /* Session being serviced */
    XDMCPSession *session;
};

G_DEFINE_TYPE (SeatXDMCPSession, seat_xdmcp_session, SEAT_TYPE);

SeatXDMCPSession *
seat_xdmcp_session_new (XDMCPSession *session)
{
    SeatXDMCPSession *seat;

    seat = g_object_new (SEAT_XDMCP_SESSION_TYPE, NULL);
    seat->priv->session = g_object_ref (session);

    return seat;
}

static Display *
seat_xdmcp_session_add_display (Seat *seat)
{
    XAuthority *authority;
    XServerRemote *xserver;

    authority = xdmcp_session_get_authority (SEAT_XDMCP_SESSION (seat)->priv->session);
    xserver = xserver_remote_new (xauth_get_address (authority), xdmcp_session_get_display_number (SEAT_XDMCP_SESSION (seat)->priv->session), authority);

    SEAT_XDMCP_SESSION (seat)->priv->display = xdisplay_new (XSERVER (xserver));
    g_object_unref (xserver);

    return DISPLAY (SEAT_XDMCP_SESSION (seat)->priv->display);
}

static void
seat_xdmcp_session_display_removed (Seat *seat, Display *display)
{
   seat_stop (seat);
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

    if (self->priv->display)
        g_object_unref (self->priv->display);
    g_object_unref (self->priv->session);

    G_OBJECT_CLASS (seat_xdmcp_session_parent_class)->finalize (object);
}

static void
seat_xdmcp_session_class_init (SeatXDMCPSessionClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    seat_class->add_display = seat_xdmcp_session_add_display;
    seat_class->display_removed = seat_xdmcp_session_display_removed;
    object_class->finalize = seat_xdmcp_session_finalize;

    g_type_class_add_private (klass, sizeof (SeatXDMCPSessionPrivate));
}
