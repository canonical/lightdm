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

#ifndef X_SERVER_REMOTE_H_
#define X_SERVER_REMOTE_H_

#include "x-server.h"

G_BEGIN_DECLS

#define X_SERVER_REMOTE_TYPE (x_server_remote_get_type())
#define X_SERVER_REMOTE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), X_SERVER_REMOTE_TYPE, XServerRemote))

typedef struct XServerRemotePrivate XServerRemotePrivate;

typedef struct
{
    XServer               parent_instance;
    XServerRemotePrivate *priv;
} XServerRemote;

typedef struct
{
    XServerClass parent_class;
} XServerRemoteClass;

GType x_server_remote_get_type (void);

XServerRemote *x_server_remote_new (const gchar *hostname, guint number, XAuthority *authority);

G_END_DECLS

#endif /* X_SERVER_REMOTE_H_ */
