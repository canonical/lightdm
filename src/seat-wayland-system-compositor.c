/*
 * Copyright (C) 2012-2013 Robert Ancell.
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

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <glib/gstdio.h>

#include "seat-wayland-system-compositor.h"
#include "configuration.h"
#include "wayland-system-compositor.h"
#include "vt.h"
#include "plymouth.h"

typedef struct
{
    /* System compositor */
    WaylandSystemCompositor *compositor;

    /* The currently visible session */
    Session *active_session;
    DisplayServer *active_display_server;
} SeatWaylandSystemCompositorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SeatWaylandSystemCompositor, seat_wayland_system_compositor, SEAT_TYPE)

static void
seat_wayland_system_compositor_setup (Seat *seat)
{
    seat_set_supports_multi_session (seat, TRUE);
    SEAT_CLASS (seat_wayland_system_compositor_parent_class)->setup (seat);
}

static void
check_stopped (SeatWaylandSystemCompositor *seat)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (seat);
    if (!priv->compositor)
        SEAT_CLASS (seat_wayland_system_compositor_parent_class)->stop (SEAT (seat));
}

static void
compositor_ready_cb (WaylandSystemCompositor *compositor, SeatWaylandSystemCompositor *seat)
{
    l_debug (seat, "Compositor ready");

    SEAT_CLASS (seat_wayland_system_compositor_parent_class)->start (SEAT (seat));
}

static void
compositor_stopped_cb (WaylandSystemCompositor *compositor, SeatWaylandSystemCompositor *seat)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (seat);

    l_debug (seat, "Compositor stopped");

    g_clear_object (&priv->compositor);

    if (seat_get_is_stopping (SEAT (seat)))
        check_stopped (seat);
    else
        seat_stop (SEAT (seat));
}

static gboolean
seat_wayland_system_compositor_start (Seat *seat)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));

    /* Replace Plymouth if it is running */
    gint vt = -1;
    if (plymouth_get_is_active () && plymouth_has_active_vt ())
    {
        gint active_vt = vt_get_active ();
        if (active_vt >= vt_get_min ())
        {
            vt = active_vt;
            plymouth_quit (TRUE);
        }
        else
            l_debug (seat, "Plymouth is running on VT %d, but this is less than the configured minimum of %d so not replacing it", active_vt, vt_get_min ());
    }
    if (plymouth_get_is_active ())
        plymouth_quit (FALSE);
    if (vt < 0)
        vt = vt_can_multi_seat () ? vt_get_unused () : 0;
    if (vt < 0)
    {
        l_debug (seat, "Failed to get a VT to run on");
        return FALSE;
    }

    int timeout = seat_get_integer_property (SEAT (seat), "wayland-compositor-timeout");
    if (timeout <= 0)
        timeout = 60;

    priv->compositor = wayland_system_compositor_new ();
    g_signal_connect (priv->compositor, DISPLAY_SERVER_SIGNAL_READY, G_CALLBACK (compositor_ready_cb), seat);
    g_signal_connect (priv->compositor, DISPLAY_SERVER_SIGNAL_STOPPED, G_CALLBACK (compositor_stopped_cb), seat);
    wayland_system_compositor_set_command (priv->compositor, seat_get_string_property (SEAT (seat), "wayland-compositor-command"));
    wayland_system_compositor_set_vt (priv->compositor, vt);
    wayland_system_compositor_set_timeout (priv->compositor, timeout);

    return display_server_start (DISPLAY_SERVER (priv->compositor));
}

static DisplayServer *
seat_wayland_system_compositor_create_display_server (Seat *seat, Session *session)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));

    const gchar *session_type = session_get_session_type (session);
    if (strcmp (session_type, "mir") == 0 || strcmp (session_type, "wayland") == 0)
        return g_object_ref (DISPLAY_SERVER (priv->compositor));
    else
    {
        l_warning (seat, "Can't create unsupported display server '%s'", session_type);
        return NULL;
    }
}

static gboolean
seat_wayland_system_compositor_display_server_is_used (Seat *seat, DisplayServer *display_server)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));

    if (display_server == DISPLAY_SERVER (priv->compositor))
        return TRUE;

    return SEAT_CLASS (seat_wayland_system_compositor_parent_class)->display_server_is_used (seat, display_server);
}

static GreeterSession *
seat_wayland_system_compositor_create_greeter_session (Seat *seat)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));

    GreeterSession *greeter_session = SEAT_CLASS (seat_wayland_system_compositor_parent_class)->create_greeter_session (seat);
    session_set_env (SESSION (greeter_session), "XDG_SEAT", seat_get_name (seat));

    gint vt = display_server_get_vt (DISPLAY_SERVER (priv->compositor));
    if (vt >= 0)
    {
        g_autofree gchar *value = g_strdup_printf ("%d", vt);
        session_set_env (SESSION (greeter_session), "XDG_VTNR", value);
    }

    return greeter_session;
}

static Session *
seat_wayland_system_compositor_create_session (Seat *seat)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));

    Session *session = SEAT_CLASS (seat_wayland_system_compositor_parent_class)->create_session (seat);
    session_set_env (session, "XDG_SEAT", seat_get_name (seat));

    gint vt = display_server_get_vt (DISPLAY_SERVER (priv->compositor));
    if (vt >= 0)
    {
        g_autofree gchar *value = g_strdup_printf ("%d", vt);
        session_set_env (SESSION (session), "XDG_VTNR", value);
    }

    return session;
}

static const gchar *
get_mir_id (Session *session)
{
    if (!session)
        return NULL;

    DisplayServer *display_server = session_get_display_server (session);
    if (IS_WAYLAND_SYSTEM_COMPOSITOR (display_server))
        return session_get_env (session, "MIR_SERVER_NAME");

    return NULL;
}

static void
seat_wayland_system_compositor_set_active_session (Seat *seat, Session *session)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));

    const gchar *old_id = get_mir_id (priv->active_session);
    const gchar *new_id = get_mir_id (session);

    g_clear_object (&priv->active_session);
    priv->active_session = g_object_ref (session);

    if (g_strcmp0 (old_id, new_id) != 0)
        wayland_system_compositor_set_active_session (priv->compositor, new_id);

    SEAT_CLASS (seat_wayland_system_compositor_parent_class)->set_active_session (seat, session);
}

static Session *
seat_wayland_system_compositor_get_active_session (Seat *seat)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));
    return priv->active_session;
}

static void
seat_wayland_system_compositor_set_next_session (Seat *seat, Session *session)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));

    if (!session)
        return;

    const gchar *id = session_get_env (session, "MIR_SERVER_NAME");

    if (id)
    {
        l_debug (seat, "Marking Mir session %s as the next session", id);
        wayland_system_compositor_set_next_session (priv->compositor, id);
    }
    else
    {
        l_debug (seat, "Failed to work out session ID to mark");
    }

    SEAT_CLASS (seat_wayland_system_compositor_parent_class)->set_next_session (seat, session);
}

static void
seat_wayland_system_compositor_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    SEAT_CLASS (seat_wayland_system_compositor_parent_class)->run_script (seat, display_server, script);
}

static void
seat_wayland_system_compositor_stop (Seat *seat)
{
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));

    /* Stop the compositor first */
    if (priv->compositor)
        display_server_stop (DISPLAY_SERVER (priv->compositor));

    check_stopped (SEAT_WAYLAND_SYSTEM_COMPOSITOR (seat));
}

static void
seat_wayland_system_compositor_init (SeatWaylandSystemCompositor *seat)
{
}

static void
seat_wayland_system_compositor_finalize (GObject *object)
{
    SeatWaylandSystemCompositor *seat = SEAT_WAYLAND_SYSTEM_COMPOSITOR (object);
    SeatWaylandSystemCompositorPrivate *priv = seat_wayland_system_compositor_get_instance_private (seat);

    g_clear_object (&priv->compositor);
    g_clear_object (&priv->active_session);
    g_clear_object (&priv->active_display_server);

    G_OBJECT_CLASS (seat_wayland_system_compositor_parent_class)->finalize (object);
}

static void
seat_wayland_system_compositor_class_init (SeatWaylandSystemCompositorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SeatClass *seat_class = SEAT_CLASS (klass);

    object_class->finalize = seat_wayland_system_compositor_finalize;
    seat_class->setup = seat_wayland_system_compositor_setup;
    seat_class->start = seat_wayland_system_compositor_start;
    seat_class->create_display_server = seat_wayland_system_compositor_create_display_server;
    seat_class->display_server_is_used = seat_wayland_system_compositor_display_server_is_used;
    seat_class->create_greeter_session = seat_wayland_system_compositor_create_greeter_session;
    seat_class->create_session = seat_wayland_system_compositor_create_session;
    seat_class->set_active_session = seat_wayland_system_compositor_set_active_session;
    seat_class->get_active_session = seat_wayland_system_compositor_get_active_session;
    seat_class->set_next_session = seat_wayland_system_compositor_set_next_session;
    seat_class->run_script = seat_wayland_system_compositor_run_script;
    seat_class->stop = seat_wayland_system_compositor_stop;
}
