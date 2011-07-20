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

#ifndef _XSERVER_REMOTE_H_
#define _XSERVER_REMOTE_H_

#include "xserver.h"

G_BEGIN_DECLS

#define XSERVER_REMOTE_TYPE (xserver_remote_get_type())
#define XSERVER_REMOTE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XSERVER_REMOTE_TYPE, XServerRemote))

typedef struct
{
    XServer parent_instance;
} XServerRemote;

typedef struct
{
    XServerClass parent_class;
} XServerRemoteClass;

GType xserver_remote_get_type (void);

XServerRemote *xserver_remote_new (const gchar *hostname, guint number, XAuthority *authority);

G_END_DECLS

#endif /* _XSERVER_REMOTE_H_ */
