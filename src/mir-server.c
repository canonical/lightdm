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

struct MirServerPrivate
{
    /* VT to run on */
    gint vt;
};

G_DEFINE_TYPE (MirServer, mir_server, DISPLAY_SERVER_TYPE);

MirServer *mir_server_new (void)
{
    return g_object_new (MIR_SERVER_TYPE, NULL);  
}

static gint
mir_server_local_get_vt (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return MIR_SERVER (server)->priv->vt;
}

static gboolean
mir_server_start (DisplayServer *display_server)
{
    return DISPLAY_SERVER_CLASS (mir_server_parent_class)->start (display_server);
}

static void
mir_server_setup_session (DisplayServer *display_server, Session *session)
{
}

static void
mir_server_init (MirServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, MIR_SERVER_TYPE, MirServerPrivate);
    server->priv->vt = -1;
}

static void
mir_server_finalize (GObject *object)
{
    G_OBJECT_CLASS (mir_server_parent_class)->finalize (object);
}

static void
mir_server_class_init (MirServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->get_vt = mir_server_local_get_vt;
    display_server_class->start = mir_server_start;
    display_server_class->setup_session = mir_server_setup_session;
    object_class->finalize = mir_server_finalize;

    g_type_class_add_private (klass, sizeof (MirServerPrivate));
}
