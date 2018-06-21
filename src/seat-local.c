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
#include "wayland-session.h"
#include "plymouth.h"
#include "vt.h"

typedef struct
{
    /* X server being used for XDMCP */
    XServerLocal *xdmcp_x_server;
} SeatLocalPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SeatLocal, seat_local, SEAT_TYPE)

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
    if (!priv->xdmcp_x_server)
        SEAT_CLASS (seat_local_parent_class)->stop (SEAT (seat));
}

static void
xdmcp_x_server_stopped_cb (DisplayServer *display_server, SeatLocal *seat)
{
    SeatLocalPrivate *priv = seat_local_get_instance_private (seat);

    l_debug (seat, "XDMCP X server stopped");

    g_signal_handlers_disconnect_matched (priv->xdmcp_x_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    g_clear_object (&priv->xdmcp_x_server);

    if (seat_get_is_stopping (SEAT (seat)))
        check_stopped (seat);
    else
        seat_stop (SEAT (seat));
}

static gboolean
seat_local_start (Seat *seat)
{
    SeatLocalPrivate *priv = seat_local_get_instance_private (SEAT_LOCAL (seat));

    /* If running as an XDMCP client then just start an X server */
    const gchar *xdmcp_manager = seat_get_string_property (seat, "xdmcp-manager");
    if (xdmcp_manager)
    {
        priv->xdmcp_x_server = create_x_server (SEAT_LOCAL (seat));
        x_server_local_set_xdmcp_server (priv->xdmcp_x_server, xdmcp_manager);
        gint port = seat_get_integer_property (seat, "xdmcp-port");
        if (port > 0)
            x_server_local_set_xdmcp_port (priv->xdmcp_x_server, port);
        const gchar *key_name = seat_get_string_property (seat, "xdmcp-key");
        if (key_name)
        {
            g_autofree gchar *path = g_build_filename (config_get_directory (config_get_instance ()), "keys.conf", NULL);

            g_autoptr(GKeyFile) keys = g_key_file_new ();
            g_autoptr(GError) error = NULL;
            gboolean result = g_key_file_load_from_file (keys, path, G_KEY_FILE_NONE, &error);
            if (error)
                l_debug (seat, "Error getting key %s", error->message);

            if (result)
            {
                g_autofree gchar *key = NULL;
                if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                    key = g_key_file_get_string (keys, "keyring", key_name, NULL);
                else
                    l_debug (seat, "Key %s not defined", key_name);

                if (key)
                    x_server_local_set_xdmcp_key (priv->xdmcp_x_server, key);
            }
        }

        g_signal_connect (priv->xdmcp_x_server, DISPLAY_SERVER_SIGNAL_STOPPED, G_CALLBACK (xdmcp_x_server_stopped_cb), seat);
        return display_server_start (DISPLAY_SERVER (priv->xdmcp_x_server));
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
    if (strcmp (seat_get_name (SEAT (seat)), "seat0") != 0)
        return -1;

    /* If Plymouth is running, stop it */
    gint vt = -1;
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

static XServerLocal *
create_x_server (SeatLocal *seat)
{
    SeatLocalPrivate *priv = seat_local_get_instance_private (seat);
    g_autoptr(XServerLocal) x_server = NULL;

    x_server = x_server_local_new ();

    gint vt = get_vt (seat, DISPLAY_SERVER (x_server));
    if (vt >= 0)
        x_server_local_set_vt (x_server, vt);

    if (vt > 0)
        l_debug (seat, "Starting local X display on VT %d", vt);
    else
        l_debug (seat, "Starting local X display");

    /* If running inside an X server use Xephyr instead */
    const gchar *command = NULL;
    if (g_getenv ("DISPLAY"))
        command = "Xephyr";
    if (!command)
        command = seat_get_string_property (SEAT (seat), "xserver-command");
    if (command)
        x_server_local_set_command (x_server, command);

    g_autofree gchar *number = g_strdup_printf ("%d", x_server_get_display_number (X_SERVER (x_server)));
    g_autoptr(XAuthority) cookie = x_authority_new_local_cookie (number);
    x_server_set_authority (X_SERVER (x_server), cookie);

    const gchar *layout = seat_get_string_property (SEAT (seat), "xserver-layout");
    if (layout)
        x_server_local_set_layout (x_server, layout);

    x_server_local_set_xdg_seat (x_server, seat_get_name (SEAT (seat)));

    const gchar *config_file = seat_get_string_property (SEAT (seat), "xserver-config");
    if (config_file)
        x_server_local_set_config (x_server, config_file);

    gboolean allow_tcp = seat_get_boolean_property (SEAT (seat), "xserver-allow-tcp");
    x_server_local_set_allow_tcp (x_server, allow_tcp);

    return g_steal_pointer (&x_server);
}

static DisplayServer *
create_wayland_session (SeatLocal *seat)
{
    g_autoptr(WaylandSession) session = wayland_session_new ();

    gint vt = get_vt (seat, DISPLAY_SERVER (session));
    if (vt >= 0)
        wayland_session_set_vt (session, vt);

    return DISPLAY_SERVER (g_steal_pointer (&session));
}

static DisplayServer *
seat_local_create_display_server (Seat *s, Session *session)
{
    SeatLocal *seat = SEAT_LOCAL (s);

    const gchar *session_type = session_get_session_type (session);
    if (strcmp (session_type, "x") == 0)
        return DISPLAY_SERVER (create_x_server (seat));
    else if (strcmp (session_type, "wayland") == 0)
        return create_wayland_session (seat);
    else
    {
        l_warning (seat, "Can't create unsupported display server '%s'", session_type);
        return NULL;
    }
}

static gboolean
seat_local_display_server_is_used (Seat *seat, DisplayServer *display_server)
{
    return SEAT_CLASS (seat_local_parent_class)->display_server_is_used (seat, display_server);
}

static GreeterSession *
seat_local_create_greeter_session (Seat *seat)
{
    GreeterSession *greeter_session = SEAT_CLASS (seat_local_parent_class)->create_greeter_session (seat);
    session_set_env (SESSION (greeter_session), "XDG_SEAT", seat_get_name (seat));

    return greeter_session;
}

static Session *
seat_local_create_session (Seat *seat)
{
    Session *session = SEAT_CLASS (seat_local_parent_class)->create_session (seat);
    session_set_env (SESSION (session), "XDG_SEAT", seat_get_name (seat));

    return session;
}

static void
seat_local_set_active_session (Seat *seat, Session *session)
{
    SeatLocalPrivate *priv = seat_local_get_instance_private (SEAT_LOCAL (seat));

    DisplayServer *display_server = session_get_display_server (session);

    gint vt = display_server_get_vt (display_server);
    if (vt >= 0)
        vt_set_active (vt);

    SEAT_CLASS (seat_local_parent_class)->set_active_session (seat, session);
}

static Session *
seat_local_get_active_session (Seat *seat)
{
    SeatLocalPrivate *priv = seat_local_get_instance_private (SEAT_LOCAL (seat));

    gint vt = vt_get_active ();
    if (vt < 0)
        return NULL;

    /* Find out which session is on this VT */
    for (GList *link = seat_get_sessions (seat); link; link = link->next)
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
seat_local_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    if (IS_X_SERVER_LOCAL (display_server))
    {
        const gchar *path = x_server_local_get_authority_file_path (X_SERVER_LOCAL (display_server));
        process_set_env (script, "DISPLAY", x_server_get_address (X_SERVER (display_server)));
        process_set_env (script, "XAUTHORITY", path);
    }

    SEAT_CLASS (seat_local_parent_class)->run_script (seat, display_server, script);
}

static void
seat_local_stop (Seat *seat)
{
    SeatLocalPrivate *priv = seat_local_get_instance_private (SEAT_LOCAL (seat));

    /* Stop the XDMCP X server */
    if (priv->xdmcp_x_server)
        display_server_stop (DISPLAY_SERVER (priv->xdmcp_x_server));

    check_stopped (SEAT_LOCAL (seat));
}

static void
seat_local_init (SeatLocal *seat)
{
}

static void
seat_local_finalize (GObject *object)
{
    SeatLocal *seat = SEAT_LOCAL (object);
    SeatLocalPrivate *priv = seat_local_get_instance_private (seat);

    if (priv->xdmcp_x_server)
        g_signal_handlers_disconnect_matched (priv->xdmcp_x_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    g_clear_object (&priv->xdmcp_x_server);

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
    seat_class->run_script = seat_local_run_script;
    seat_class->stop = seat_local_stop;
}
