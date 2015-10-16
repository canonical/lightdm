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

#include "mir-server.h"
#include "configuration.h"
#include "vt.h"

struct MirServerPrivate
{
    /* VT to run on */
    gint vt;

    /* Mir socket for this server to talk to parent */
    gchar *parent_socket;
};

G_DEFINE_TYPE (MirServer, mir_server, DISPLAY_SERVER_TYPE);

MirServer *mir_server_new (void)
{
    return g_object_new (MIR_SERVER_TYPE, NULL);
}

void
mir_server_set_vt (MirServer *server, gint vt)
{
    g_return_if_fail (server != NULL);
    if (server->priv->vt > 0)
        vt_unref (server->priv->vt);
    server->priv->vt = vt;
    if (vt > 0)
        vt_ref (vt);
}

void
mir_server_set_parent_socket (MirServer *server, const gchar *parent_socket)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->parent_socket);
    server->priv->parent_socket = g_strdup (parent_socket);
}

static const gchar *
mir_server_get_session_type (DisplayServer *server)
{
    return "mir";
}

static gint
mir_server_get_vt (DisplayServer *server)
{
    return MIR_SERVER (server)->priv->vt;
}

static void
mir_server_connect_session (DisplayServer *display_server, Session *session)
{
    MirServer *server;

    session_set_env (session, "XDG_SESSION_TYPE", "mir");

    server = MIR_SERVER (display_server);
    if (server->priv->parent_socket)
        session_set_env (session, "MIR_SOCKET", server->priv->parent_socket);
    if (server->priv->vt > 0)
    {
        gchar *value = g_strdup_printf ("%d", server->priv->vt);
        session_set_env (session, "MIR_SERVER_VT", value);
        g_free (value);
    }
}

static void
mir_server_disconnect_session (DisplayServer *display_server, Session *session)
{
    session_unset_env (session, "XDG_SESSION_TYPE");
    session_unset_env (session, "MIR_SOCKET");
    session_unset_env (session, "MIR_SERVER_VT");
}

static void
mir_server_init (MirServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, MIR_SERVER_TYPE, MirServerPrivate);
    server->priv->vt = -1;
    display_server_set_name (DISPLAY_SERVER (server), "mir");
}

static void
mir_server_finalize (GObject *object)
{
    MirServer *self = MIR_SERVER (object);

    if (self->priv->vt > 0)
        vt_unref (self->priv->vt);
    g_free (self->priv->parent_socket);

    G_OBJECT_CLASS (mir_server_parent_class)->finalize (object);
}

static void
mir_server_class_init (MirServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->get_session_type = mir_server_get_session_type;
    display_server_class->get_vt = mir_server_get_vt;
    display_server_class->connect_session = mir_server_connect_session;
    display_server_class->disconnect_session = mir_server_disconnect_session;
    object_class->finalize = mir_server_finalize;

    g_type_class_add_private (klass, sizeof (MirServerPrivate));
}
