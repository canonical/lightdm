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
#include "mir-session.h"

struct MirServerPrivate
{  
};

G_DEFINE_TYPE (MirServer, mir_server, DISPLAY_SERVER_TYPE);

static gboolean
mir_server_start (DisplayServer *display_server)
{
    return DISPLAY_SERVER_CLASS (mir_server_parent_class)->start (display_server);
}

static void
mir_server_init (MirServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, MIR_SERVER_TYPE, MirServerPrivate);
}

static void
mir_server_finalize (GObject *object)
{
    MirServer *self;

    self = MIR_SERVER (object);

    G_OBJECT_CLASS (mir_server_parent_class)->finalize (object);
}

static void
mir_server_class_init (MirServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->start = mir_server_start;
    object_class->finalize = mir_server_finalize;

    g_type_class_add_private (klass, sizeof (MirServerPrivate));
}
