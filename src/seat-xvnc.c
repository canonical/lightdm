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

#include "seat-xvnc.h"
#include "x-server-xvnc.h"
#include "configuration.h"
#include "accounts.h"

typedef struct
{
    /* VNC connection */
    GSocket *connection;

    /* X server using VNC connection */
    XServerXVNC *x_server;
} SeatXVNCPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SeatXVNC, seat_xvnc, SEAT_TYPE)

SeatXVNC *seat_xvnc_new (GSocket *connection)
{
    SeatXVNC *seat = g_object_new (SEAT_XVNC_TYPE, NULL);
    SeatXVNCPrivate *priv = seat_xvnc_get_instance_private (seat);

    priv->connection = g_object_ref (connection);

    return seat;
}

static void
seat_xvnc_setup (Seat *seat)
{
    seat_set_supports_multi_session (seat, FALSE);
    SEAT_CLASS (seat_xvnc_parent_class)->setup (seat);
}

static DisplayServer *
seat_xvnc_create_display_server (Seat *seat, Session *session)
{
    SeatXVNCPrivate *priv = seat_xvnc_get_instance_private (SEAT_XVNC (seat));

    if (strcmp (session_get_session_type (session), "x") != 0)
        return NULL;

    /* Can only create one server for the lifetime of this seat (can't re-use VNC connection) */
    if (priv->x_server)
        return NULL;

    g_autoptr(XServerXVNC) x_server = x_server_xvnc_new ();
    priv->x_server = g_object_ref (x_server);
    g_autofree gchar *number = g_strdup_printf ("%d", x_server_get_display_number (X_SERVER (x_server)));
    g_autoptr(XAuthority) cookie = x_authority_new_local_cookie (number);
    x_server_set_authority (X_SERVER (x_server), cookie);
    x_server_xvnc_set_socket (x_server, g_socket_get_fd (priv->connection));

    const gchar *command = config_get_string (config_get_instance (), "VNCServer", "command");
    if (command)
        x_server_local_set_command (X_SERVER_LOCAL (x_server), command);

    const gchar *username = config_get_string (config_get_instance (), "VNCServer", "user");
    if (username)
    {
        g_autoptr(User) user = accounts_get_user_by_name (username);
        if (user)
            x_server_local_set_user (X_SERVER_LOCAL (x_server), user);
        else
            l_warning(seat, "Unable to lookup records for user %s (will default to running user)", username);
    }

    if (config_has_key (config_get_instance (), "VNCServer", "width") &&
        config_has_key (config_get_instance (), "VNCServer", "height"))
    {
        gint width, height;
        width = config_get_integer (config_get_instance (), "VNCServer", "width");
        height = config_get_integer (config_get_instance (), "VNCServer", "height");
        if (height > 0 && width > 0)
            x_server_xvnc_set_geometry (x_server, width, height);
    }
    if (config_has_key (config_get_instance (), "VNCServer", "depth"))
    {
        gint depth;
        depth = config_get_integer (config_get_instance (), "VNCServer", "depth");
        if (depth == 8 || depth == 16 || depth == 24 || depth == 32)
            x_server_xvnc_set_depth (x_server, depth);
    }

    return DISPLAY_SERVER (g_steal_pointer (&x_server));
}

static void
seat_xvnc_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    SeatXVNCPrivate *priv = seat_xvnc_get_instance_private (SEAT_XVNC (seat));
    XServerXVNC *x_server = X_SERVER_XVNC (display_server);

    GInetSocketAddress *address = G_INET_SOCKET_ADDRESS (g_socket_get_remote_address (priv->connection, NULL));
    g_autofree gchar *hostname = g_inet_address_to_string (g_inet_socket_address_get_address (address));
    const gchar *path = x_server_local_get_authority_file_path (X_SERVER_LOCAL (x_server));

    process_set_env (script, "REMOTE_HOST", hostname);
    process_set_env (script, "DISPLAY", x_server_get_address (X_SERVER (x_server)));
    process_set_env (script, "XAUTHORITY", path);

    SEAT_CLASS (seat_xvnc_parent_class)->run_script (seat, display_server, script);
}

static void
seat_xvnc_init (SeatXVNC *seat)
{
}

static void
seat_xvnc_session_finalize (GObject *object)
{
    SeatXVNC *self = SEAT_XVNC (object);
    SeatXVNCPrivate *priv = seat_xvnc_get_instance_private (self);

    g_clear_object (&priv->connection);
    g_clear_object (&priv->x_server);

    G_OBJECT_CLASS (seat_xvnc_parent_class)->finalize (object);
}

static void
seat_xvnc_class_init (SeatXVNCClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    seat_class->setup = seat_xvnc_setup;
    seat_class->create_display_server = seat_xvnc_create_display_server;
    seat_class->run_script = seat_xvnc_run_script;
    object_class->finalize = seat_xvnc_session_finalize;
}
