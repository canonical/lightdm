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
#include "xserver-remote.h"
#include "xsession.h"

G_DEFINE_TYPE (SeatXRemote, seat_xremote, SEAT_TYPE);

static void
seat_xremote_setup (Seat *seat)
{
    seat_set_can_switch (seat, FALSE);
    SEAT_CLASS (seat_xremote_parent_class)->setup (seat);
}

static DisplayServer *
seat_xremote_create_display_server (Seat *seat)
{
    XServerRemote *xserver;
    const gchar *hostname;
    gint number;

    hostname = seat_get_string_property (seat, "xserver-hostname");
    if (!hostname)
        hostname = "localhost";
    number = seat_get_integer_property (seat, "xserver-display-number");

    g_debug ("Starting remote X display %s:%d", hostname, number);

    xserver = xserver_remote_new (hostname, number, NULL);

    return DISPLAY_SERVER (xserver);
}

static Session *
seat_xremote_create_session (Seat *seat, DisplayServer *display_server)
{
    XServerRemote *xserver;
    XSession *session;

    xserver = XSERVER_REMOTE (display_server);

    session = xsession_new ();
    session_set_remote_host_name (SESSION (session), xserver_get_hostname (XSERVER (xserver)));

    return SESSION (session);
}

static void
seat_xremote_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    XServerRemote *xserver;

    xserver = XSERVER_REMOTE (display_server);
    process_set_env (script, "DISPLAY", xserver_get_address (XSERVER (xserver)));  
    process_set_env (script, "REMOTE_HOST", xserver_get_hostname (XSERVER (xserver)));

    SEAT_CLASS (seat_xremote_parent_class)->run_script (seat, display_server, script);
}

static void
seat_xremote_init (SeatXRemote *seat)
{
}

static void
seat_xremote_class_init (SeatXRemoteClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);

    seat_class->setup = seat_xremote_setup;
    seat_class->create_display_server = seat_xremote_create_display_server;
    seat_class->create_session = seat_xremote_create_session;
    seat_class->run_script = seat_xremote_run_script;
}
