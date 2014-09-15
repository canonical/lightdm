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

#include "seat-xlocal.h"
#include "configuration.h"
#include "x-server-local.h"
#include "unity-system-compositor.h"
#include "plymouth.h"
#include "vt.h"

struct SeatXLocalPrivate
{
    /* X server being used for XDMCP */
    XServerLocal *xdmcp_x_server;
};

G_DEFINE_TYPE (SeatXLocal, seat_xlocal, SEAT_TYPE);

static XServerLocal *create_x_server (Seat *seat);

static void
seat_xlocal_setup (Seat *seat)
{
    seat_set_supports_multi_session (seat, TRUE);
    seat_set_share_display_server (seat, seat_get_boolean_property (seat, "xserver-share"));
    SEAT_CLASS (seat_xlocal_parent_class)->setup (seat);
}

static void
check_stopped (SeatXLocal *seat)
{
    if (!seat->priv->xdmcp_x_server)
        SEAT_CLASS (seat_xlocal_parent_class)->stop (SEAT (seat));
}

static void
xdmcp_x_server_stopped_cb (DisplayServer *display_server, Seat *seat)
{
    l_debug (seat, "XDMCP X server stopped");

    g_signal_handlers_disconnect_matched (SEAT_XLOCAL (seat)->priv->xdmcp_x_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    SEAT_XLOCAL (seat)->priv->xdmcp_x_server = NULL;
    g_object_unref (display_server);
    
    if (seat_get_is_stopping (seat))
        check_stopped (SEAT_XLOCAL (seat));
    else
        seat_stop (seat);
}

static gboolean
seat_xlocal_start (Seat *seat)
{
    const gchar *xdmcp_manager = NULL;

    /* If running as an XDMCP client then just start an X server */
    xdmcp_manager = seat_get_string_property (seat, "xdmcp-manager");
    if (xdmcp_manager)
    {
        SeatXLocal *s = SEAT_XLOCAL (seat);
        const gchar *key_name = NULL;
        gint port = 0;

        s->priv->xdmcp_x_server = create_x_server (seat);
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

        g_signal_connect (s->priv->xdmcp_x_server, "stopped", G_CALLBACK (xdmcp_x_server_stopped_cb), seat);
        return display_server_start (DISPLAY_SERVER (s->priv->xdmcp_x_server));
    }

    return SEAT_CLASS (seat_xlocal_parent_class)->start (seat);
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
get_vt (Seat *seat, DisplayServer *display_server)
{
    gint vt = -1;
    const gchar *xdg_seat = seat_get_name (seat);

    if (strcmp (xdg_seat, "seat0") != 0)
        return vt;

    /* If Plymouth is running, stop it */
    if (plymouth_get_is_active () && plymouth_has_active_vt ())
    {
        gint active_vt = vt_get_active ();
        if (active_vt >= vt_get_min ())
        {
            vt = active_vt;
            g_signal_connect (display_server, "ready", G_CALLBACK (display_server_ready_cb), seat);
            g_signal_connect (display_server, "stopped", G_CALLBACK (display_server_transition_plymouth_cb), seat);
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
create_x_server (Seat *seat)
{
    XServerLocal *x_server;
    const gchar *command = NULL, *layout = NULL, *config_file = NULL;
    gboolean allow_tcp;
    gint vt;

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
        command = seat_get_string_property (seat, "xserver-command");
    if (command)
        x_server_local_set_command (x_server, command);

    layout = seat_get_string_property (seat, "xserver-layout");
    if (layout)
        x_server_local_set_layout (x_server, layout);

    x_server_local_set_xdg_seat (x_server, seat_get_name (seat));
    if (strcmp (seat_get_name (seat), "seat0") != 0)
        x_server_local_set_sharevts (x_server, TRUE);

    config_file = seat_get_string_property (seat, "xserver-config");
    if (config_file)
        x_server_local_set_config (x_server, config_file);
  
    allow_tcp = seat_get_boolean_property (seat, "xserver-allow-tcp");
    x_server_local_set_allow_tcp (x_server, allow_tcp);    

    return x_server;
}

static DisplayServer *
create_unity_system_compositor (Seat *seat)
{
    UnitySystemCompositor *compositor;
    const gchar *command;
    gchar *socket_name;
    gint vt, timeout, i;

    compositor = unity_system_compositor_new ();

    command = seat_get_string_property (seat, "unity-compositor-command");
    if (command)
        unity_system_compositor_set_command (compositor, command);

    timeout = seat_get_integer_property (seat, "unity-compositor-timeout");
    if (timeout <= 0)
        timeout = 60;
    unity_system_compositor_set_timeout (compositor, timeout);

    vt = get_vt (seat, DISPLAY_SERVER (compositor));
    if (vt >= 0)
        unity_system_compositor_set_vt (compositor, vt);
  
    for (i = 0; ; i++)
    {
        socket_name = g_strdup_printf ("/run/lightdm-mir-%d", i);
        if (!g_file_test (socket_name, G_FILE_TEST_EXISTS))
            break;
    }
    unity_system_compositor_set_socket (compositor, socket_name);
    g_free (socket_name);

    unity_system_compositor_set_enable_hardware_cursor (compositor, TRUE);

    return DISPLAY_SERVER (compositor);
}

static DisplayServer *
seat_xlocal_create_display_server (Seat *seat, Session *session)
{
    const gchar *session_type;

    session_type = session_get_session_type (session);
    if (strcmp (session_type, "x") == 0)
        return DISPLAY_SERVER (create_x_server (seat));
    else if (strcmp (session_type, "mir") == 0)
        return create_unity_system_compositor (seat);
    else if (strcmp (session_type, "mir-container") == 0)
    {
        DisplayServer *compositor;
        const gchar *compositor_command;

        compositor = create_unity_system_compositor (seat);
        compositor_command = session_config_get_compositor_command (session_get_config (session));
        if (compositor_command)
            unity_system_compositor_set_command (UNITY_SYSTEM_COMPOSITOR (compositor), compositor_command);
      
        return compositor;
    }
    else
    {
        l_warning (seat, "Can't create unsupported display server '%s'", session_type);
        return NULL;
    }
}

static Greeter *
seat_xlocal_create_greeter_session (Seat *seat)
{
    Greeter *greeter_session;

    greeter_session = SEAT_CLASS (seat_xlocal_parent_class)->create_greeter_session (seat);
    session_set_env (SESSION (greeter_session), "XDG_SEAT", seat_get_name (seat));

    return greeter_session;
}

static Session *
seat_xlocal_create_session (Seat *seat)
{
    Session *session;

    session = SEAT_CLASS (seat_xlocal_parent_class)->create_session (seat);
    session_set_env (SESSION (session), "XDG_SEAT", seat_get_name (seat));

    return session;
}

static void
seat_xlocal_set_active_session (Seat *seat, Session *session)
{
    DisplayServer *display_server;

    display_server = session_get_display_server (session);

    gint vt = display_server_get_vt (display_server);
    if (vt >= 0)
        vt_set_active (vt);

    if (IS_UNITY_SYSTEM_COMPOSITOR (display_server))
        unity_system_compositor_set_active_session (UNITY_SYSTEM_COMPOSITOR (display_server), IS_GREETER (session) ? "greeter-0" : "session-0");

    SEAT_CLASS (seat_xlocal_parent_class)->set_active_session (seat, session);
}

static Session *
seat_xlocal_get_active_session (Seat *seat)
{
    gint vt;
    GList *link;

    vt = vt_get_active ();
    if (vt < 0)
        return NULL;

    for (link = seat_get_sessions (seat); link; link = link->next)
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
seat_xlocal_run_script (Seat *seat, DisplayServer *display_server, Process *script)
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

    SEAT_CLASS (seat_xlocal_parent_class)->run_script (seat, display_server, script);
}

static void
seat_xlocal_stop (Seat *seat)
{
    /* Stop the XDMCP X server first */
    if (SEAT_XLOCAL (seat)->priv->xdmcp_x_server)
        display_server_stop (DISPLAY_SERVER (SEAT_XLOCAL (seat)->priv->xdmcp_x_server));

    check_stopped (SEAT_XLOCAL (seat));
}

static void
seat_xlocal_init (SeatXLocal *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_XLOCAL_TYPE, SeatXLocalPrivate);
}

static void
seat_xlocal_finalize (GObject *object)
{
    SeatXLocal *seat = SEAT_XLOCAL (object);

    if (seat->priv->xdmcp_x_server)
    {
        g_signal_handlers_disconnect_matched (seat->priv->xdmcp_x_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
        g_object_unref (seat->priv->xdmcp_x_server);
    }

    G_OBJECT_CLASS (seat_xlocal_parent_class)->finalize (object);
}

static void
seat_xlocal_class_init (SeatXLocalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SeatClass *seat_class = SEAT_CLASS (klass);

    object_class->finalize = seat_xlocal_finalize;

    seat_class->setup = seat_xlocal_setup;
    seat_class->start = seat_xlocal_start;
    seat_class->create_display_server = seat_xlocal_create_display_server;
    seat_class->create_greeter_session = seat_xlocal_create_greeter_session;
    seat_class->create_session = seat_xlocal_create_session;
    seat_class->set_active_session = seat_xlocal_set_active_session;
    seat_class->get_active_session = seat_xlocal_get_active_session;
    seat_class->run_script = seat_xlocal_run_script;
    seat_class->stop = seat_xlocal_stop;

    g_type_class_add_private (klass, sizeof (SeatXLocalPrivate));
}
