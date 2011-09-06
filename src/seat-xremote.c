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

#include "seat-xremote.h"
#include "configuration.h"
#include "xdisplay.h"
#include "xserver-remote.h"
#include "vt.h"

G_DEFINE_TYPE (SeatXRemote, seat_xremote, SEAT_TYPE);

struct SeatXRemotePrivate
{
    /* Display being controlled by this seat */
    XDisplay *display;
};

static void
seat_xremote_setup (Seat *seat)
{
    seat_set_can_switch (seat, FALSE);
    SEAT_CLASS (seat_xremote_parent_class)->setup (seat);
}

static Display *
seat_xremote_add_display (Seat *seat)
{
    XServerRemote *xserver;
    const gchar *hostname;
    gint number;

    /* Can only have one display */
    if (SEAT_XREMOTE (seat)->priv->display)
        return NULL;

    hostname = seat_get_string_property (seat, "xserver-hostname");
    if (!hostname)
        hostname = "localhost";
    number = seat_get_integer_property (seat, "xserver-display-number");

    g_debug ("Starting remote X display %s:%d", hostname, number);

    xserver = xserver_remote_new (hostname, number, NULL);

    SEAT_XREMOTE (seat)->priv->display = xdisplay_new (XSERVER (xserver));
    g_object_unref (xserver);
  
    return DISPLAY (SEAT_XREMOTE (seat)->priv->display);
}

static void
seat_xremote_display_removed (Seat *seat, Display *display)
{
    /* Can't restart the display, so remove this seat */
    seat_stop (seat);
}

static void
seat_xremote_init (SeatXRemote *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_XREMOTE_TYPE, SeatXRemotePrivate);
}

static void
seat_xremote_class_init (SeatXRemoteClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);

    seat_class->setup = seat_xremote_setup;
    seat_class->add_display = seat_xremote_add_display;
    seat_class->display_removed = seat_xremote_display_removed;

    g_type_class_add_private (klass, sizeof (SeatXRemotePrivate));
}
