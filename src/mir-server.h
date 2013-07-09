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

#ifndef MIR_SERVER_H_
#define MIR_SERVER_H_

#include <glib-object.h>
#include "display-server.h"

G_BEGIN_DECLS

#define MIR_SERVER_TYPE (mir_server_get_type())
#define MIR_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), MIR_SERVER_TYPE, MirServer))
#define MIR_SERVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), MIR_SERVER_TYPE, MirServerClass))
#define MIR_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), MIR_SERVER_TYPE, MirServerClass))
#define IS_MIR_SERVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MIR_SERVER_TYPE))

typedef struct MirServerPrivate MirServerPrivate;

typedef struct
{
    DisplayServer     parent_instance;
    MirServerPrivate *priv;
} MirServer;

typedef struct
{
    DisplayServerClass parent_class;
} MirServerClass;

GType mir_server_get_type (void);

G_END_DECLS

#endif /* MIR_SERVER_H_ */
