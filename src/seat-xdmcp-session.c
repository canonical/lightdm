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
#include "x-server-remote.h"

typedef struct
{
    /* Session being serviced */
    XDMCPSession *session;

    /* X server using XDMCP connection */
    XServerRemote *x_server;
} SeatXDMCPSessionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SeatXDMCPSession, seat_xdmcp_session, SEAT_TYPE)

SeatXDMCPSession *
seat_xdmcp_session_new (XDMCPSession *session)
{
    SeatXDMCPSession *seat = g_object_new (SEAT_XDMCP_SESSION_TYPE, NULL);
    SeatXDMCPSessionPrivate *priv = seat_xdmcp_session_get_instance_private (seat);

    priv->session = g_object_ref (session);

    return seat;
}

static DisplayServer *
seat_xdmcp_session_create_display_server (Seat *seat, Session *session)
{
    SeatXDMCPSessionPrivate *priv = seat_xdmcp_session_get_instance_private (SEAT_XDMCP_SESSION (seat));

    if (strcmp (session_get_session_type (session), "x") != 0)
        return NULL;

    /* Only create one server for the lifetime of this seat (XDMCP clients reconnect on logout) */
    if (priv->x_server)
        return NULL;

    XAuthority *authority = xdmcp_session_get_authority (priv->session);
    g_autofree gchar *host = g_inet_address_to_string (xdmcp_session_get_address (priv->session));

    priv->x_server = x_server_remote_new (host, xdmcp_session_get_display_number (priv->session), authority);

    return g_object_ref (DISPLAY_SERVER (priv->x_server));
}

static void
seat_xdmcp_session_init (SeatXDMCPSession *seat)
{
}

static void
seat_xdmcp_session_finalize (GObject *object)
{
    SeatXDMCPSession *self = SEAT_XDMCP_SESSION (object);
    SeatXDMCPSessionPrivate *priv = seat_xdmcp_session_get_instance_private (self);

    g_clear_object (&priv->session);
    g_clear_object (&priv->x_server);

    G_OBJECT_CLASS (seat_xdmcp_session_parent_class)->finalize (object);
}

static void
seat_xdmcp_session_class_init (SeatXDMCPSessionClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    seat_class->create_display_server = seat_xdmcp_session_create_display_server;
    object_class->finalize = seat_xdmcp_session_finalize;
}
