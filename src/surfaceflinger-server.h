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

#ifndef SURFACEFLINGER_SERVER_H_
#define SURFACEFLINGER_SERVER_H_

#include <glib-object.h>
#include "display-server.h"

G_BEGIN_DECLS

#define SURFACEFLINGER_SERVER_TYPE (surfaceflinger_server_get_type())
#define SURFACEFLINGER_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SURFACEFLINGER_SERVER_TYPE, SurfaceflingerServer))
#define SURFACEFLINGER_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), SURFACEFLINGER_SERVER_TYPE, SurfaceflingerServerClass))
#define SURFACEFLINGER_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SURFACEFLINGER_SERVER_TYPE, SurfaceflingerServerClass))
#define IS_SURFACEFLINGER_SERVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SURFACEFLINGER_SERVER_TYPE))

typedef struct SurfaceflingerServerPrivate SurfaceflingerServerPrivate;

typedef struct
{
    DisplayServer     parent_instance;
    SurfaceflingerServerPrivate *priv;
} SurfaceflingerServer;

typedef struct
{
    DisplayServerClass parent_class;
} SurfaceflingerServerClass;

GType surfaceflinger_server_get_type (void);

SurfaceflingerServer *surfaceflinger_server_new (void);

const gchar *surfaceflinger_server_get_id (SurfaceflingerServer *server);

G_END_DECLS

#endif /* SURFACEFLINGER_SERVER_H_ */
