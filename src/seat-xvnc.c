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
#include "xserver-xvnc.h"
#include "xsession.h"

G_DEFINE_TYPE (SeatXVNC, seat_xvnc, SEAT_TYPE);

struct SeatXVNCPrivate
{
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

static DisplayServer *
seat_xvnc_create_display_server (Seat *seat)
{
    XServerXVNC *xserver;

    xserver = xserver_xvnc_new ();
    xserver_xvnc_set_socket (xserver, g_socket_get_fd (SEAT_XVNC (seat)->priv->connection));

    return DISPLAY_SERVER (xserver);
}

static Session *
seat_xvnc_create_session (Seat *seat, Display *display)
{
    XServerXVNC *xserver;
    XSession *session;
    GInetSocketAddress *address;
    gchar *hostname;

    xserver = XSERVER_XVNC (display_get_display_server (display));

    session = xsession_new (XSERVER (xserver));
    address = G_INET_SOCKET_ADDRESS (g_socket_get_remote_address (SEAT_XVNC (seat)->priv->connection, NULL));
    hostname = g_inet_address_to_string (g_inet_socket_address_get_address (address));
    session_set_console_kit_parameter (SESSION (session), "remote-host-name", g_variant_new_string (hostname));
    g_free (hostname);
    session_set_console_kit_parameter (SESSION (session), "is-local", g_variant_new_boolean (FALSE));

    return SESSION (session);
}

static void
seat_xvnc_run_script (Seat *seat, Display *display, Process *script)
{
    XServerXVNC *xserver;
    GInetSocketAddress *address;
    gchar *hostname;
    gchar *path;

    xserver = XSERVER_XVNC (display_get_display_server (display));

    address = G_INET_SOCKET_ADDRESS (g_socket_get_remote_address (SEAT_XVNC (seat)->priv->connection, NULL));
    hostname = g_inet_address_to_string (g_inet_socket_address_get_address (address));
    path = xserver_xvnc_get_authority_file_path (xserver);

    process_set_env (script, "REMOTE_HOST", hostname);
    process_set_env (script, "DISPLAY", xserver_get_address (XSERVER (xserver)));
    process_set_env (script, "XAUTHORITY", path);

    g_free (hostname);
    g_free (path);

    SEAT_CLASS (seat_xvnc_parent_class)->run_script (seat, display, script);
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

    g_object_unref (self->priv->connection);

    G_OBJECT_CLASS (seat_xvnc_parent_class)->finalize (object);
}

static void
seat_xvnc_class_init (SeatXVNCClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    seat_class->create_display_server = seat_xvnc_create_display_server;
    seat_class->create_session = seat_xvnc_create_session;
    seat_class->run_script = seat_xvnc_run_script;
    seat_class->display_removed = seat_xvnc_display_removed;
    object_class->finalize = seat_xdmcp_session_finalize;

    g_type_class_add_private (klass, sizeof (SeatXVNCPrivate));
}
