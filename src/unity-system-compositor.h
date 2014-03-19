/*
 * Copyright (C) 2013 Canonical Ltd.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef UNITY_SYSTEM_COMPOSITOR_H_
#define UNITY_SYSTEM_COMPOSITOR_H_

#include <glib-object.h>
#include "display-server.h"

G_BEGIN_DECLS

#define UNITY_SYSTEM_COMPOSITOR_TYPE    (unity_system_compositor_get_type())
#define UNITY_SYSTEM_COMPOSITOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), UNITY_SYSTEM_COMPOSITOR_TYPE, UnitySystemCompositor))
#define IS_UNITY_SYSTEM_COMPOSITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), UNITY_SYSTEM_COMPOSITOR_TYPE))

typedef struct UnitySystemCompositorPrivate UnitySystemCompositorPrivate;

typedef struct
{
    DisplayServer                 parent_instance;
    UnitySystemCompositorPrivate *priv;
} UnitySystemCompositor;

typedef struct
{
    DisplayServerClass parent_class;

    void (*ready)(UnitySystemCompositor *compositor);
} UnitySystemCompositorClass;

GType unity_system_compositor_get_type (void);

UnitySystemCompositor *unity_system_compositor_new (void);

void unity_system_compositor_set_command (UnitySystemCompositor *compositor, const gchar *command);

void unity_system_compositor_set_socket (UnitySystemCompositor *compositor, const gchar *socket);

const gchar *unity_system_compositor_get_socket (UnitySystemCompositor *compositor);

void unity_system_compositor_set_vt (UnitySystemCompositor *compositor, gint vt);

void unity_system_compositor_set_enable_hardware_cursor (UnitySystemCompositor *compositor, gboolean enable_cursor);

void unity_system_compositor_set_timeout (UnitySystemCompositor *compositor, gint timeout);

void unity_system_compositor_set_active_session (UnitySystemCompositor *compositor, const gchar *id);

void unity_system_compositor_set_next_session (UnitySystemCompositor *compositor, const gchar *id);

G_END_DECLS

#endif /* UNITY_SYSTEM_COMPOSITOR_H_ */
