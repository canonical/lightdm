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
    /* TRUE if stopping this seat (waiting for displays to stop) */
    gboolean stopping;
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
    XDisplay *display;
    const gchar *hostname;
    gint number;

    hostname = seat_get_string_property (seat, "xserver-hostname");
    if (!hostname)
        hostname = "localhost";
    number = seat_get_integer_property (seat, "xserver-display-number");

    g_debug ("Starting remote X display %s:%d", hostname, number);

    xserver = xserver_remote_new (hostname, number, NULL);

    display = xdisplay_new (XSERVER (xserver));
    g_object_unref (xserver);
  
    return DISPLAY (display);
}

static void
seat_xremote_display_removed (Seat *seat, Display *display)
{
    SeatXRemotePrivate *priv = SEAT_XREMOTE (seat)->priv;

    /* Show a new greeter */
    if (!priv->stopping && display == seat_get_active_display (seat))
    {
        g_debug ("Active display stopped, switching to greeter");
        seat_switch_to_greeter (seat);
    }
}

static void
seat_xremote_stop (Seat *seat)
{
    SEAT_XREMOTE (seat)->priv->stopping = TRUE;
    SEAT_CLASS (seat_xremote_parent_class)->stop (seat);
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
    seat_class->stop = seat_xremote_stop;

    g_type_class_add_private (klass, sizeof (SeatXRemotePrivate));
}
