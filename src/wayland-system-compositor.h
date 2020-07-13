/*
 * Copyright (C) 2013 Canonical Ltd.
 * Copyright (C) 2020 UBports Foundation.
 * Author(s): Robert Ancell <robert.ancell@canonical.com>
 *            Marius Gripsgard <marius@ubports.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef WAYLAND_SYSTEM_COMPOSITOR_H_
#define WAYLAND_SYSTEM_COMPOSITOR_H_

#include <glib-object.h>
#include "display-server.h"

G_BEGIN_DECLS

#define WAYLAND_SYSTEM_COMPOSITOR_TYPE    (wayland_system_compositor_get_type())
#define WAYLAND_SYSTEM_COMPOSITOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), WAYLAND_SYSTEM_COMPOSITOR_TYPE, WaylandSystemCompositor))
#define IS_WAYLAND_SYSTEM_COMPOSITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), WAYLAND_SYSTEM_COMPOSITOR_TYPE))

typedef struct
{
    DisplayServer parent_instance;
} WaylandSystemCompositor;

typedef struct
{
    DisplayServerClass parent_class;
} WaylandSystemCompositorClass;

GType wayland_system_compositor_get_type (void);

WaylandSystemCompositor *wayland_system_compositor_new (void);

void wayland_system_compositor_set_command (WaylandSystemCompositor *compositor, const gchar *command);

void wayland_system_compositor_set_socket (WaylandSystemCompositor *compositor, const gchar *socket);

const gchar *wayland_system_compositor_get_socket (WaylandSystemCompositor *compositor);

void wayland_system_compositor_set_vt (WaylandSystemCompositor *compositor, gint vt);

void wayland_system_compositor_set_timeout (WaylandSystemCompositor *compositor, gint timeout);

void wayland_system_compositor_set_active_session (WaylandSystemCompositor *compositor, const gchar *id);

void wayland_system_compositor_set_next_session (WaylandSystemCompositor *compositor, const gchar *id);

G_END_DECLS

#endif /* WAYLAND_SYSTEM_COMPOSITOR_H_ */
