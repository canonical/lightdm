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
#include "x-server-remote.h"

G_DEFINE_TYPE (SeatXRemote, seat_xremote, SEAT_TYPE)

static DisplayServer *
seat_xremote_create_display_server (Seat *seat, Session *session)
{
    const gchar *session_type = session_get_session_type (session);
    if (strcmp (session_type, "x") != 0)
    {
        l_warning (seat, "X remote seat only supports X display servers, not '%s'", session_type);
        return NULL;
    }

    const gchar *hostname = seat_get_string_property (seat, "xserver-hostname");
    gint number = seat_get_integer_property (seat, "xserver-display-number");

    l_debug (seat, "Starting remote X display %s:%d", hostname ? hostname : "", number);

    return DISPLAY_SERVER (x_server_remote_new (hostname, number, NULL));
}

static GreeterSession *
seat_xremote_create_greeter_session (Seat *seat)
{
    GreeterSession *greeter_session = SEAT_CLASS (seat_xremote_parent_class)->create_greeter_session (seat);
    session_set_env (SESSION (greeter_session), "XDG_SEAT", seat_get_name (seat));

    return greeter_session;
}

static Session *
seat_xremote_create_session (Seat *seat)
{
    Session *session = SEAT_CLASS (seat_xremote_parent_class)->create_session (seat);
    session_set_env (SESSION (session), "XDG_SEAT", seat_get_name (seat));

    return session;
}

static void
seat_xremote_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    XServerRemote *x_server = X_SERVER_REMOTE (display_server);
    process_set_env (script, "DISPLAY", x_server_get_address (X_SERVER (x_server)));
    process_set_env (script, "REMOTE_HOST", x_server_get_hostname (X_SERVER (x_server)));

    SEAT_CLASS (seat_xremote_parent_class)->run_script (seat, display_server, script);
}

static void
seat_xremote_init (SeatXRemote *seat)
{
    seat_set_supports_multi_session (SEAT (seat), FALSE);
}

static void
seat_xremote_class_init (SeatXRemoteClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);

    seat_class->create_display_server = seat_xremote_create_display_server;
    seat_class->create_greeter_session = seat_xremote_create_greeter_session;
    seat_class->create_session = seat_xremote_create_session;
    seat_class->run_script = seat_xremote_run_script;
}
