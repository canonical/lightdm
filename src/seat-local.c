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

#include <string.h>

#include "seat-local.h"
#include "configuration.h"
#include "x-server-local.h"
#include "x-server-xmir.h"
#include "unity-system-compositor.h"
#include "wayland-session.h"
#include "plymouth.h"
#include "vt.h"

struct SeatLocalPrivate
{
    /* System compositor being used for Mir sessions */
    UnitySystemCompositor *compositor;

    /* Session currently active on compositor */
    Session *active_compositor_session;

    /* Counter for Mir IDs to use */
    int next_xmir_id;

    /* X server being used for XDMCP */
    XServerLocal *xdmcp_x_server;
};

G_DEFINE_TYPE (SeatLocal, seat_local, SEAT_TYPE);

static XServerLocal *create_x_server (SeatLocal *seat);

static void
seat_local_setup (Seat *seat)
{
    seat_set_supports_multi_session (seat, TRUE);
    seat_set_share_display_server (seat, seat_get_boolean_property (seat, "xserver-share"));
    SEAT_CLASS (seat_local_parent_class)->setup (seat);
}

static void
check_stopped (SeatLocal *seat)
{
    if (!seat->priv->compositor && !seat->priv->xdmcp_x_server)
        SEAT_CLASS (seat_local_parent_class)->stop (SEAT (seat));
}

static void
xdmcp_x_server_stopped_cb (DisplayServer *display_server, SeatLocal *seat)
{
    l_debug (seat, "XDMCP X server stopped");

    g_signal_handlers_disconnect_matched (seat->priv->xdmcp_x_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    g_clear_object (&seat->priv->xdmcp_x_server);

    if (seat_get_is_stopping (SEAT (seat)))
        check_stopped (seat);
    else
        seat_stop (SEAT (seat));
}

static void
compositor_stopped_cb (UnitySystemCompositor *compositor, SeatLocal *seat)
{
    l_debug (seat, "Compositor stopped");

    g_clear_object (&seat->priv->compositor);

    if (seat_get_is_stopping (SEAT (seat)))
        check_stopped (seat);
}  

static gboolean
seat_local_start (Seat *seat)
{
    const gchar *xdmcp_manager = NULL;

    /* If running as an XDMCP client then just start an X server */
    xdmcp_manager = seat_get_string_property (seat, "xdmcp-manager");
    if (xdmcp_manager)
    {
        SeatLocal *s = SEAT_LOCAL (seat);
        const gchar *key_name = NULL;
        gint port = 0;

        s->priv->xdmcp_x_server = create_x_server (s);
        x_server_local_set_xdmcp_server (s->priv->xdmcp_x_server, xdmcp_manager);
        port = seat_get_integer_property (seat, "xdmcp-port");
        if (port > 0)
            x_server_local_set_xdmcp_port (s->priv->xdmcp_x_server, port);
        key_name = seat_get_string_property (seat, "xdmcp-key");
        if (key_name)
        {
            gchar *path;
            GKeyFile *keys;
            gboolean result;
            GError *error = NULL;

            path = g_build_filename (config_get_directory (config_get_instance ()), "keys.conf", NULL);

            keys = g_key_file_new ();
            result = g_key_file_load_from_file (keys, path, G_KEY_FILE_NONE, &error);
            if (error)
                l_debug (seat, "Error getting key %s", error->message);
            g_clear_error (&error);

            if (result)
            {
                gchar *key = NULL;

                if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                    key = g_key_file_get_string (keys, "keyring", key_name, NULL);
                else
                    l_debug (seat, "Key %s not defined", key_name);

                if (key)
                    x_server_local_set_xdmcp_key (s->priv->xdmcp_x_server, key);
                g_free (key);
            }

            g_free (path);
            g_key_file_free (keys);
        }

        g_signal_connect (s->priv->xdmcp_x_server, DISPLAY_SERVER_SIGNAL_STOPPED, G_CALLBACK (xdmcp_x_server_stopped_cb), seat);
        return display_server_start (DISPLAY_SERVER (s->priv->xdmcp_x_server));
    }

    return SEAT_CLASS (seat_local_parent_class)->start (seat);
}

static void
display_server_ready_cb (DisplayServer *display_server, Seat *seat)
{
    /* Quit Plymouth */
    plymouth_quit (TRUE);
}

static void
display_server_transition_plymouth_cb (DisplayServer *display_server, Seat *seat)
{
    /* Quit Plymouth if we didn't do the transition */
    if (plymouth_get_is_running ())
        plymouth_quit (FALSE);

    g_signal_handlers_disconnect_matched (display_server, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, display_server_transition_plymouth_cb, NULL);
}

static gint
get_vt (SeatLocal *seat, DisplayServer *display_server)
{
    gint vt = -1;
    const gchar *xdg_seat = seat_get_name (SEAT (seat));

    if (strcmp (xdg_seat, "seat0") != 0)
        return vt;

    /* If Plymouth is running, stop it */
    if (plymouth_get_is_active () && plymouth_has_active_vt ())
    {
        gint active_vt = vt_get_active ();
        if (active_vt >= vt_get_min ())
        {
            vt = active_vt;
            g_signal_connect (display_server, DISPLAY_SERVER_SIGNAL_READY, G_CALLBACK (display_server_ready_cb), seat);
            g_signal_connect (display_server, DISPLAY_SERVER_SIGNAL_STOPPED, G_CALLBACK (display_server_transition_plymouth_cb), seat);
            plymouth_deactivate ();
        }
        else
            l_debug (seat, "Plymouth is running on VT %d, but this is less than the configured minimum of %d so not replacing it", active_vt, vt_get_min ());
    }
    if (plymouth_get_is_active ())
        plymouth_quit (FALSE);
    if (vt < 0)
        vt = vt_get_unused ();

    return vt;
}

static UnitySystemCompositor *
create_unity_system_compositor (SeatLocal *seat)
{
    UnitySystemCompositor *compositor;
    const gchar *command;
    gint timeout, vt;

    compositor = unity_system_compositor_new ();

    command = seat_get_string_property (SEAT (seat), "unity-compositor-command");
    if (command)
        unity_system_compositor_set_command (compositor, command);

    timeout = seat_get_integer_property (SEAT (seat), "unity-compositor-timeout");
    if (timeout <= 0)
        timeout = 60;
    unity_system_compositor_set_timeout (compositor, timeout);

    vt = get_vt (seat, DISPLAY_SERVER (compositor));
    if (vt >= 0)
        unity_system_compositor_set_vt (compositor, vt);

    return compositor;
}

static UnitySystemCompositor *
get_unity_system_compositor (SeatLocal *seat)
{
    if (seat->priv->compositor)
        return seat->priv->compositor;

    seat->priv->compositor = create_unity_system_compositor (seat);
    seat->priv->next_xmir_id = 0;
    g_signal_connect (seat->priv->compositor, DISPLAY_SERVER_SIGNAL_STOPPED, G_CALLBACK (compositor_stopped_cb), seat);

    return seat->priv->compositor;
}

static XServerLocal *
create_x_server (SeatLocal *seat)
{
    const gchar *x_server_backend;
    XServerLocal *x_server;
    gchar *number;
    XAuthority *cookie;
    const gchar *layout = NULL, *config_file = NULL;
    gboolean allow_tcp;
    gint vt;

    x_server_backend = seat_get_string_property (SEAT (seat), "xserver-backend");
    if (g_strcmp0 (x_server_backend, "mir") == 0)
    {
        UnitySystemCompositor *compositor;
        const gchar *command;
        gchar *id;

        compositor = get_unity_system_compositor (SEAT_LOCAL (seat));
        x_server = X_SERVER_LOCAL (x_server_xmir_new (compositor));

        command = seat_get_string_property (SEAT (seat), "xmir-command");
        if (command)
            x_server_local_set_command (x_server, command);

        id = g_strdup_printf ("x-%d", seat->priv->next_xmir_id);
        seat->priv->next_xmir_id++;
        x_server_xmir_set_mir_id (X_SERVER_XMIR (x_server), id);
        x_server_xmir_set_mir_socket (X_SERVER_XMIR (x_server), unity_system_compositor_get_socket (compositor));
        g_free (id);
    }
    else
    {
        const gchar *command = NULL;

        x_server = x_server_local_new ();

        vt = get_vt (seat, DISPLAY_SERVER (x_server));
        if (vt >= 0)
            x_server_local_set_vt (x_server, vt);

        if (vt > 0)
            l_debug (seat, "Starting local X display on VT %d", vt);
        else
            l_debug (seat, "Starting local X display");

        /* If running inside an X server use Xephyr instead */
        if (g_getenv ("DISPLAY"))
            command = "Xephyr";
        if (!command)
            command = seat_get_string_property (SEAT (seat), "xserver-command");
        if (command)
            x_server_local_set_command (x_server, command);
    }

    number = g_strdup_printf ("%d", x_server_get_display_number (X_SERVER (x_server)));
    cookie = x_authority_new_local_cookie (number);
    x_server_set_authority (X_SERVER (x_server), cookie);
    g_free (number);
    g_object_unref (cookie);

    layout = seat_get_string_property (SEAT (seat), "xserver-layout");
    if (layout)
        x_server_local_set_layout (x_server, layout);

    x_server_local_set_xdg_seat (x_server, seat_get_name (SEAT (seat)));

    config_file = seat_get_string_property (SEAT (seat), "xserver-config");
    if (config_file)
        x_server_local_set_config (x_server, config_file);

    allow_tcp = seat_get_boolean_property (SEAT (seat), "xserver-allow-tcp");
    x_server_local_set_allow_tcp (x_server, allow_tcp);

    return x_server;
}

static DisplayServer *
create_wayland_session (SeatLocal *seat)
{
    WaylandSession *session;
    gint vt;

    session = wayland_session_new ();

    vt = get_vt (seat, DISPLAY_SERVER (session));
    if (vt >= 0)
        wayland_session_set_vt (session, vt);

    return DISPLAY_SERVER (session);
}

static DisplayServer *
seat_local_create_display_server (Seat *s, Session *session)
{
    SeatLocal *seat = SEAT_LOCAL (s);
    const gchar *session_type;

    session_type = session_get_session_type (session);
    if (strcmp (session_type, "x") == 0)
        return DISPLAY_SERVER (create_x_server (seat));
    else if (strcmp (session_type, "mir") == 0)
        return g_object_ref (get_unity_system_compositor (seat));
    else if (strcmp (session_type, "wayland") == 0)
        return create_wayland_session (seat);
    else if (strcmp (session_type, "mir-container") == 0)
    {
        UnitySystemCompositor *compositor;
        const gchar *compositor_command;

        compositor = create_unity_system_compositor (seat);
        compositor_command = session_config_get_compositor_command (session_get_config (session));
        if (compositor_command)
            unity_system_compositor_set_command (compositor, compositor_command);

        return DISPLAY_SERVER (compositor);
    }
    else
    {
        l_warning (seat, "Can't create unsupported display server '%s'", session_type);
        return NULL;
    }
}

static gboolean
seat_local_display_server_is_used (Seat *seat, DisplayServer *display_server)
{
    if (display_server == DISPLAY_SERVER (SEAT_LOCAL (seat)->priv->compositor))
        return TRUE;

    return SEAT_CLASS (seat_local_parent_class)->display_server_is_used (seat, display_server);
}

static GreeterSession *
seat_local_create_greeter_session (Seat *seat)
{
    GreeterSession *greeter_session;

    greeter_session = SEAT_CLASS (seat_local_parent_class)->create_greeter_session (seat);
    session_set_env (SESSION (greeter_session), "XDG_SEAT", seat_get_name (seat));

    return greeter_session;
}

static Session *
seat_local_create_session (Seat *seat)
{
    Session *session;

    session = SEAT_CLASS (seat_local_parent_class)->create_session (seat);
    session_set_env (SESSION (session), "XDG_SEAT", seat_get_name (seat));

    return session;
}

static void
seat_local_set_active_session (Seat *s, Session *session)
{
    SeatLocal *seat = SEAT_LOCAL (s);
    DisplayServer *display_server;

    display_server = session_get_display_server (session);

    gint vt = display_server_get_vt (display_server);
    if (vt >= 0)
        vt_set_active (vt);

    g_clear_object (&seat->priv->active_compositor_session);
    if (IS_UNITY_SYSTEM_COMPOSITOR (display_server))
    {
        unity_system_compositor_set_active_session (UNITY_SYSTEM_COMPOSITOR (display_server), session_get_env (session, "MIR_SERVER_NAME"));
        seat->priv->active_compositor_session = g_object_ref (session);
    }
    if (IS_X_SERVER_XMIR (display_server))
    {
        unity_system_compositor_set_active_session (seat->priv->compositor, x_server_xmir_get_mir_id (X_SERVER_XMIR (display_server)));
        seat->priv->active_compositor_session = g_object_ref (session);
    }

    SEAT_CLASS (seat_local_parent_class)->set_active_session (s, session);
}

static Session *
seat_local_get_active_session (Seat *s)
{
    SeatLocal *seat = SEAT_LOCAL (s);
    gint vt;
    GList *link;

    vt = vt_get_active ();
    if (vt < 0)
        return NULL;

    /* If the compositor is active return the session it is displaying */
    if (seat->priv->compositor && display_server_get_vt (DISPLAY_SERVER (seat->priv->compositor)) == vt)
        return seat->priv->active_compositor_session;

    /* Otherwise find out which session is on this VT */
    for (link = seat_get_sessions (s); link; link = link->next)
    {
        Session *session = link->data;
        DisplayServer *display_server;

        display_server = session_get_display_server (session);
        if (display_server && display_server_get_vt (display_server) == vt)
            return session;
    }

    return NULL;
}

static void
seat_local_set_next_session (Seat *seat, Session *session)
{
    DisplayServer *display_server;
    const gchar *id = NULL;

    if (!session)
        return;

    display_server = session_get_display_server (session);

    if (IS_X_SERVER_XMIR (display_server))
        id = x_server_xmir_get_mir_id (X_SERVER_XMIR (display_server));
    else
        id = session_get_env (session, "MIR_SERVER_NAME");

    if (id)
    {
        l_debug (seat, "Marking Mir session %s as the next session", id);
        unity_system_compositor_set_next_session (SEAT_LOCAL (seat)->priv->compositor, id);
    }
    else
        l_debug (seat, "Failed to work out session ID to mark");

    SEAT_CLASS (seat_local_parent_class)->set_next_session (seat, session);
}

static void
seat_local_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    if (IS_X_SERVER_LOCAL (display_server))
    {
        const gchar *path;
        XServerLocal *x_server;

        x_server = X_SERVER_LOCAL (display_server);
        path = x_server_local_get_authority_file_path (x_server);
        process_set_env (script, "DISPLAY", x_server_get_address (X_SERVER (x_server)));
        process_set_env (script, "XAUTHORITY", path);
    }

    SEAT_CLASS (seat_local_parent_class)->run_script (seat, display_server, script);
}

static void
seat_local_stop (Seat *s)
{
    SeatLocal *seat = SEAT_LOCAL (s);

    /* Stop the compositor */
    if (seat->priv->compositor)
        display_server_stop (DISPLAY_SERVER (seat->priv->compositor));

    /* Stop the XDMCP X server */
    if (seat->priv->xdmcp_x_server)
        display_server_stop (DISPLAY_SERVER (seat->priv->xdmcp_x_server));

    check_stopped (seat);
}

static void
seat_local_init (SeatLocal *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_LOCAL_TYPE, SeatLocalPrivate);
}

static void
seat_local_finalize (GObject *object)
{
    SeatLocal *seat = SEAT_LOCAL (object);

    g_clear_object (&seat->priv->compositor);
    g_clear_object (&seat->priv->active_compositor_session);
    if (seat->priv->xdmcp_x_server)
    {
        g_signal_handlers_disconnect_matched (seat->priv->xdmcp_x_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
        g_object_unref (seat->priv->xdmcp_x_server);
    }

    G_OBJECT_CLASS (seat_local_parent_class)->finalize (object);
}

static void
seat_local_class_init (SeatLocalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SeatClass *seat_class = SEAT_CLASS (klass);

    object_class->finalize = seat_local_finalize;

    seat_class->setup = seat_local_setup;
    seat_class->start = seat_local_start;
    seat_class->create_display_server = seat_local_create_display_server;
    seat_class->display_server_is_used = seat_local_display_server_is_used;
    seat_class->create_greeter_session = seat_local_create_greeter_session;
    seat_class->create_session = seat_local_create_session;
    seat_class->set_active_session = seat_local_set_active_session;
    seat_class->get_active_session = seat_local_get_active_session;
    seat_class->set_next_session = seat_local_set_next_session;
    seat_class->run_script = seat_local_run_script;
    seat_class->stop = seat_local_stop;

    g_type_class_add_private (klass, sizeof (SeatLocalPrivate));
}
