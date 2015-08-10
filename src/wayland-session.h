/*
 * Copyright (C) 2015 Canonical Ltd.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef WAYLAND_SESSION_H_
#define WAYLAND_SESSION_H_

#include <glib-object.h>
#include "display-server.h"

G_BEGIN_DECLS

#define WAYLAND_SESSION_TYPE    (wayland_session_get_type())
#define WAYLAND_SESSION(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), WAYLAND_SESSION_TYPE, WaylandSession))
#define IS_WAYLAND_SESSION(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), WAYLAND_SESSION_TYPE))

typedef struct WaylandSessionPrivate WaylandSessionPrivate;

typedef struct
{
    DisplayServer          parent_instance;
    WaylandSessionPrivate *priv;
} WaylandSession;

typedef struct
{
    DisplayServerClass parent_class;
} WaylandSessionClass;

GType wayland_session_get_type (void);

WaylandSession *wayland_session_new (void);

void wayland_session_set_vt (WaylandSession *session, gint vt);

G_END_DECLS

#endif /* WAYLAND_SESSION_H_ */
