/*
 * Copyright (C) 2013 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <string.h>

#include "seat-surfaceflinger.h"
#include "surfaceflinger-server.h"
#include "vt.h"

G_DEFINE_TYPE (SeatSurfaceflinger, seat_surfaceflinger, SEAT_TYPE);

static void
seat_surfaceflinger_setup (Seat *seat)
{
    seat_set_can_switch (seat, FALSE);
    SEAT_CLASS (seat_surfaceflinger_parent_class)->setup (seat);
}

static DisplayServer *
seat_surfaceflinger_create_display_server (Seat *seat, const gchar *session_type)
{
    /* Allow mir types too, because Mir sessions usually support surfaceflinger
       as an alternate mode, since Mir is frequently used on phones. */
    if (strcmp (session_type, "surfaceflinger") == 0 || strcmp (session_type, "mir") == 0)
        return DISPLAY_SERVER (surfaceflinger_server_new ());
    else
    {
        l_warning (seat, "Can't create unsupported display server '%s'", session_type);
        return NULL;
    }
}

static Greeter *
seat_surfaceflinger_create_greeter_session (Seat *seat)
{
    Greeter *greeter_session;
    const gchar *xdg_seat;

    greeter_session = SEAT_CLASS (seat_surfaceflinger_parent_class)->create_greeter_session (seat);
    xdg_seat = seat_get_string_property (seat, "xdg-seat");
    if (!xdg_seat)
        xdg_seat = "seat0";
    session_set_env (SESSION (greeter_session), "XDG_SEAT", xdg_seat);

    /* Fake the VT */
    session_set_env (SESSION (greeter_session), "XDG_VTNR", vt_can_multi_seat() ? "1" : "0");

    return greeter_session;
}

static Session *
seat_surfaceflinger_create_session (Seat *seat)
{
    Session *session;
    const gchar *xdg_seat;

    session = SEAT_CLASS (seat_surfaceflinger_parent_class)->create_session (seat);
    xdg_seat = seat_get_string_property (seat, "xdg-seat");
    if (!xdg_seat)
        xdg_seat = "seat0";
    session_set_env (session, "XDG_SEAT", xdg_seat);

    /* Fake the VT */
    session_set_env (session, "XDG_VTNR", vt_can_multi_seat() ? "1" : "0");

    return session;
}

static void
seat_surfaceflinger_init (SeatSurfaceflinger *seat)
{
}

static void
seat_surfaceflinger_class_init (SeatSurfaceflingerClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);

    seat_class->setup = seat_surfaceflinger_setup;
    seat_class->create_display_server = seat_surfaceflinger_create_display_server;
    seat_class->create_greeter_session = seat_surfaceflinger_create_greeter_session;
    seat_class->create_session = seat_surfaceflinger_create_session;
}
