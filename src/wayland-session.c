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

#include "wayland-session.h"
#include "vt.h"

typedef struct
{
    /* VT to run on */
    gint vt;
    gboolean have_vt_ref;
} WaylandSessionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (WaylandSession, wayland_session, DISPLAY_SERVER_TYPE)

WaylandSession *
wayland_session_new (void)
{
    return g_object_new (WAYLAND_SESSION_TYPE, NULL);
}

void
wayland_session_set_vt (WaylandSession *session, gint vt)
{
    WaylandSessionPrivate *priv = wayland_session_get_instance_private (session);

    g_return_if_fail (session != NULL);

    if (priv->have_vt_ref)
        vt_unref (priv->vt);
    priv->have_vt_ref = FALSE;
    priv->vt = vt;
    if (vt > 0)
    {
        vt_ref (vt);
        priv->have_vt_ref = TRUE;
    }
}

static gint
wayland_session_get_vt (DisplayServer *server)
{
    WaylandSessionPrivate *priv = wayland_session_get_instance_private (WAYLAND_SESSION (server));
    g_return_val_if_fail (server != NULL, 0);
    return priv->vt;
}

static void
wayland_session_connect_session (DisplayServer *display_server, Session *session)
{
    WaylandSession *wayland_session = WAYLAND_SESSION (display_server);
    WaylandSessionPrivate *priv = wayland_session_get_instance_private (wayland_session);

    session_set_env (session, "XDG_SESSION_TYPE", "wayland");

    if (priv->vt >= 0)
    {
        g_autofree gchar *value = g_strdup_printf ("%d", priv->vt);
        session_set_env (session, "XDG_VTNR", value);
    }
}

static void
wayland_session_disconnect_session (DisplayServer *display_server, Session *session)
{
    session_unset_env (session, "XDG_SESSION_TYPE");
    session_unset_env (session, "XDG_VTNR");
}

static void
wayland_session_init (WaylandSession *session)
{
    WaylandSessionPrivate *priv = wayland_session_get_instance_private (session);
    priv->vt = -1;
}

static void
wayland_session_finalize (GObject *object)
{
    WaylandSession *self = WAYLAND_SESSION (object);
    WaylandSessionPrivate *priv = wayland_session_get_instance_private (self);

    if (priv->have_vt_ref)
        vt_unref (priv->vt);

    G_OBJECT_CLASS (wayland_session_parent_class)->finalize (object);
}

static void
wayland_session_class_init (WaylandSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->get_vt = wayland_session_get_vt;
    display_server_class->connect_session = wayland_session_connect_session;
    display_server_class->disconnect_session = wayland_session_disconnect_session;
    object_class->finalize = wayland_session_finalize;
}
