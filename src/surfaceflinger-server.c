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

#include "surfaceflinger-server.h"

G_DEFINE_TYPE (SurfaceflingerServer, surfaceflinger_server, DISPLAY_SERVER_TYPE);

SurfaceflingerServer *surfaceflinger_server_new (void)
{
    return g_object_new (SURFACEFLINGER_SERVER_TYPE, NULL);  
}

static const gchar *
surfaceflinger_server_get_session_type (DisplayServer *server)
{
    return "surfaceflinger";
}

static void
surfaceflinger_server_init (SurfaceflingerServer *server)
{
    display_server_set_name (DISPLAY_SERVER (server), "sf");
}

static void
surfaceflinger_server_class_init (SurfaceflingerServerClass *klass)
{
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->get_session_type = surfaceflinger_server_get_session_type;
}
