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
#include "xgreeter.h"
#include "xsession.h"
#include "configuration.h"

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
    const gchar *command = NULL;

    xserver = xserver_xvnc_new ();
    xserver_xvnc_set_socket (xserver, g_socket_get_fd (SEAT_XVNC (seat)->priv->connection));

    command = config_get_string (config_get_instance (), "VNCServer", "command");
    if (command)
        xserver_xvnc_set_command (xserver, command);

    if (config_has_key (config_get_instance (), "VNCServer", "width") &&
        config_has_key (config_get_instance (), "VNCServer", "height"))
    {
        gint width, height;
        width = config_get_integer (config_get_instance (), "VNCServer", "width");
        height = config_get_integer (config_get_instance (), "VNCServer", "height");
        if (height > 0 && width > 0)
            xserver_xvnc_set_geometry (xserver, width, height);
    }
    if (config_has_key (config_get_instance (), "VNCServer", "depth"))
    {
        gint depth;
        depth = config_get_integer (config_get_instance (), "VNCServer", "depth");
        if (depth == 8 || depth == 16 || depth == 24 || depth == 32)
            xserver_xvnc_set_depth (xserver, depth);
    }

    return DISPLAY_SERVER (xserver);
}

static Greeter *
seat_xvnc_create_greeter_session (Seat *seat)
{
    return GREETER (xgreeter_new ());
}

static Session *
seat_xvnc_create_session (Seat *seat)
{
    return SESSION (xsession_new ());
}

static void
seat_xvnc_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    XServerXVNC *xserver;
    GInetSocketAddress *address;
    gchar *hostname;
    const gchar *path;

    xserver = XSERVER_XVNC (display_server);

    address = G_INET_SOCKET_ADDRESS (g_socket_get_remote_address (SEAT_XVNC (seat)->priv->connection, NULL));
    hostname = g_inet_address_to_string (g_inet_socket_address_get_address (address));
    path = xserver_xvnc_get_authority_file_path (xserver);

    process_set_env (script, "REMOTE_HOST", hostname);
    process_set_env (script, "DISPLAY", xserver_get_address (XSERVER (xserver)));
    process_set_env (script, "XAUTHORITY", path);

    g_free (hostname);

    SEAT_CLASS (seat_xvnc_parent_class)->run_script (seat, display_server, script);
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
    seat_class->create_greeter_session = seat_xvnc_create_greeter_session;
    seat_class->create_session = seat_xvnc_create_session;
    seat_class->run_script = seat_xvnc_run_script;
    object_class->finalize = seat_xdmcp_session_finalize;

    g_type_class_add_private (klass, sizeof (SeatXVNCPrivate));
}
