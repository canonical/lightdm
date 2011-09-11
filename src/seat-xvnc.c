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

#include "seat-xvnc.h"
#include "xdisplay.h"
#include "xserver-xvnc.h"

G_DEFINE_TYPE (SeatXVNC, seat_xvnc, SEAT_TYPE);

struct SeatXVNCPrivate
{
    /* Remote display */
    XDisplay *display;

    /* VNC connection */
    GSocket *connection;
};

SeatXVNC *seat_xvnc_new (GSocket *connection)
{
    SeatXVNC *seat;

    seat = g_object_new (SEAT_XVNC_TYPE, NULL);
    seat->priv->connection = g_object_ref (connection);

    return seat;
}

static Display *
seat_xvnc_add_display (Seat *seat)
{
    XServerXVNC *xserver;

    xserver = xserver_xvnc_new ();
    xserver_xvnc_set_stdin (xserver, g_socket_get_fd (SEAT_XVNC (seat)->priv->connection));

    SEAT_XVNC (seat)->priv->display = xdisplay_new (XSERVER (xserver));
    g_object_unref (xserver);

    return DISPLAY (SEAT_XVNC (seat)->priv->display);  
}

static void
seat_xvnc_display_removed (Seat *seat, Display *display)
{
    seat_stop (seat);
}

static void
seat_xvnc_init (SeatXVNC *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_XVNC_TYPE, SeatXVNCPrivate);
}

static void
seat_xdmcp_session_finalize (GObject *object)
{
    SeatXVNC *self;

    self = SEAT_XVNC (object);

    if (self->priv->display)
        g_object_unref (self->priv->display);
    g_object_unref (self->priv->connection);

    G_OBJECT_CLASS (seat_xvnc_parent_class)->finalize (object);
}

static void
seat_xvnc_class_init (SeatXVNCClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    seat_class->add_display = seat_xvnc_add_display;
    seat_class->display_removed = seat_xvnc_display_removed;
    object_class->finalize = seat_xdmcp_session_finalize;

    g_type_class_add_private (klass, sizeof (SeatXVNCPrivate));
}
