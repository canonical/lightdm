/*
 * Copyright (C) 2012-2013 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
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

#include "seat-unity.h"
#include "configuration.h"
#include "unity-system-compositor.h"
#include "x-server-local.h"
#include "mir-server.h"
#include "vt.h"
#include "plymouth.h"

struct SeatUnityPrivate
{
    /* System compositor */
    UnitySystemCompositor *compositor;

    /* Next Mir ID to use for a Mir sessions, X server and greeters */
    gint next_session_id;
    gint next_x_server_id;
    gint next_greeter_id;

    /* The currently visible session */
    Session *active_session;
    DisplayServer *active_display_server;
};

G_DEFINE_TYPE (SeatUnity, seat_unity, SEAT_TYPE);

static gboolean
seat_unity_get_start_local_sessions (Seat *seat)
{
    return !seat_get_string_property (seat, "xdmcp-manager");
}

static void
seat_unity_setup (Seat *seat)
{
    seat_set_can_switch (seat, TRUE);
    SEAT_CLASS (seat_unity_parent_class)->setup (seat);
}

static void
compositor_ready_cb (UnitySystemCompositor *compositor, SeatUnity *seat)
{
    l_debug (seat, "Compositor ready"); 
    SEAT_CLASS (seat_unity_parent_class)->start (SEAT (seat));
}

static void
compositor_stopped_cb (UnitySystemCompositor *compositor, SeatUnity *seat)
{
    g_object_unref (seat->priv->compositor);
    seat->priv->compositor = NULL;

    if (seat_get_is_stopping (SEAT (seat)))
    {
        SEAT_CLASS (seat_unity_parent_class)->stop (SEAT (seat));
        return;
    }

    l_debug (seat, "Stopping Unity seat, compositor terminated");

    seat_stop (SEAT (seat));
}

static gboolean
seat_unity_start (Seat *seat)
{
    gint vt = -1;
    int timeout;

    /* Replace Plymouth if it is running */
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

    timeout = seat_get_integer_property (seat, "unity-compositor-timeout");
    if (timeout <= 0)
        timeout = 60;

    SEAT_UNITY (seat)->priv->compositor = unity_system_compositor_new ();
    g_signal_connect (SEAT_UNITY (seat)->priv->compositor, "ready", G_CALLBACK (compositor_ready_cb), seat);
    g_signal_connect (SEAT_UNITY (seat)->priv->compositor, "stopped", G_CALLBACK (compositor_stopped_cb), seat);
    unity_system_compositor_set_command (SEAT_UNITY (seat)->priv->compositor, seat_get_string_property (seat, "unity-compositor-command"));
    unity_system_compositor_set_vt (SEAT_UNITY (seat)->priv->compositor, vt);
    unity_system_compositor_set_timeout (SEAT_UNITY (seat)->priv->compositor, timeout);

    return display_server_start (DISPLAY_SERVER (SEAT_UNITY (seat)->priv->compositor));
}

static DisplayServer *
create_x_server (Seat *seat)
{
    XServerLocal *x_server;
    const gchar *command = NULL, *layout = NULL, *config_file = NULL, *xdmcp_manager = NULL, *key_name = NULL;
    gboolean allow_tcp;
    gint port = 0;
    gchar *id;

    l_debug (seat, "Starting X server on Unity compositor");

    x_server = x_server_local_new ();

    command = seat_get_string_property (seat, "xserver-command");
    if (command)
        x_server_local_set_command (x_server, command);

    id = g_strdup_printf ("x-%d", SEAT_UNITY (seat)->priv->next_x_server_id);
    SEAT_UNITY (seat)->priv->next_x_server_id++;
    x_server_local_set_mir_id (x_server, id);
    x_server_local_set_mir_socket (x_server, unity_system_compositor_get_socket (SEAT_UNITY (seat)->priv->compositor));
    g_free (id);

    layout = seat_get_string_property (seat, "xserver-layout");
    if (layout)
        x_server_local_set_layout (x_server, layout);
    
    x_server_local_set_xdg_seat (x_server, seat_get_name (seat));

    config_file = seat_get_string_property (seat, "xserver-config");
    if (config_file)
        x_server_local_set_config (x_server, config_file);

    allow_tcp = seat_get_boolean_property (seat, "xserver-allow-tcp");
    x_server_local_set_allow_tcp (x_server, allow_tcp);

    xdmcp_manager = seat_get_string_property (seat, "xdmcp-manager");
    if (xdmcp_manager)
        x_server_local_set_xdmcp_server (x_server, xdmcp_manager);

    port = seat_get_integer_property (seat, "xdmcp-port");
    if (port > 0)
        x_server_local_set_xdmcp_port (x_server, port);

    key_name = seat_get_string_property (seat, "xdmcp-key");
    if (key_name)
    {
        gchar *dir, *path;
        GKeyFile *keys;
        gboolean result;
        GError *error = NULL;

        dir = config_get_string (config_get_instance (), "LightDM", "config-directory");
        path = g_build_filename (dir, "keys.conf", NULL);
        g_free (dir);

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
                x_server_local_set_xdmcp_key (x_server, key);
            g_free (key);
        }

        g_free (path);
        g_key_file_free (keys);
    }

    return DISPLAY_SERVER (x_server);
}

static DisplayServer *
create_mir_server (Seat *seat)
{
    MirServer *mir_server;

    mir_server = mir_server_new ();
    mir_server_set_parent_socket (mir_server, unity_system_compositor_get_socket (SEAT_UNITY (seat)->priv->compositor));

    return DISPLAY_SERVER (mir_server);
}

static DisplayServer *
seat_unity_create_display_server (Seat *seat, const gchar *session_type)
{  
    if (strcmp (session_type, "x") == 0)
        return create_x_server (seat);
    else if (strcmp (session_type, "mir") == 0)
        return create_mir_server (seat);
    else
    {
        l_warning (seat, "Can't create unsupported display server '%s'", session_type);
        return NULL;
    }
}

static Greeter *
seat_unity_create_greeter_session (Seat *seat)
{
    Greeter *greeter_session;
    gchar *id;
    gint vt;

    greeter_session = SEAT_CLASS (seat_unity_parent_class)->create_greeter_session (seat);
    session_set_env (SESSION (greeter_session), "XDG_SEAT", seat_get_name (seat));

    id = g_strdup_printf ("greeter-%d", SEAT_UNITY (seat)->priv->next_greeter_id);
    SEAT_UNITY (seat)->priv->next_greeter_id++;
    session_set_env (SESSION (greeter_session), "MIR_SERVER_NAME", id);
    g_free (id);

    vt = display_server_get_vt (DISPLAY_SERVER (SEAT_UNITY (seat)->priv->compositor));
    if (vt >= 0)
    {
        gchar *value = g_strdup_printf ("%d", vt);
        l_debug (seat, "Setting XDG_VTNR=%s", value);
        session_set_env (SESSION (greeter_session), "XDG_VTNR", value);
        g_free (value);
    }
    else
        l_debug (seat, "Not setting XDG_VTNR");

    return greeter_session;
}

static Session *
seat_unity_create_session (Seat *seat)
{
    Session *session;
    gchar *id;
    gint vt;

    session = SEAT_CLASS (seat_unity_parent_class)->create_session (seat);
    session_set_env (session, "XDG_SEAT", seat_get_name (seat));

    id = g_strdup_printf ("session-%d", SEAT_UNITY (seat)->priv->next_session_id);
    SEAT_UNITY (seat)->priv->next_session_id++;
    session_set_env (session, "MIR_SERVER_NAME", id);
    g_free (id);

    vt = display_server_get_vt (DISPLAY_SERVER (SEAT_UNITY (seat)->priv->compositor));
    if (vt >= 0)
    {
        gchar *value = g_strdup_printf ("%d", vt);
        l_debug (seat, "Setting XDG_VTNR=%s", value);
        session_set_env (SESSION (session), "XDG_VTNR", value);
        g_free (value);
    }
    else
        l_debug (seat, "Not setting XDG_VTNR");

    return session;
}

static void
seat_unity_set_active_session (Seat *seat, Session *session)
{
    DisplayServer *display_server;

    if (session == SEAT_UNITY (seat)->priv->active_session)
        return;
    SEAT_UNITY (seat)->priv->active_session = g_object_ref (session);

    display_server = session_get_display_server (session);
    if (SEAT_UNITY (seat)->priv->active_display_server != display_server)
    {
        const gchar *id = NULL;

        SEAT_UNITY (seat)->priv->active_display_server = g_object_ref (display_server);

        if (IS_X_SERVER_LOCAL (display_server))
            id = x_server_local_get_mir_id (X_SERVER_LOCAL (display_server));
        else
            id = session_get_env (session, "MIR_SERVER_NAME");

        if (id)
        {
            l_debug (seat, "Switching to Mir session %s", id);
            unity_system_compositor_set_active_session (SEAT_UNITY (seat)->priv->compositor, id);
        }
        else
            l_warning (seat, "Failed to work out session ID");
    }

    SEAT_CLASS (seat_unity_parent_class)->set_active_session (seat, session);
}

static Session *
seat_unity_get_active_session (Seat *seat)
{
    return SEAT_UNITY (seat)->priv->active_session;
}

static void
seat_unity_set_next_session (Seat *seat, Session *session)
{
    DisplayServer *display_server;
    const gchar *id = NULL;

    if (!session)
        return;

    display_server = session_get_display_server (session);

    if (IS_X_SERVER_LOCAL (display_server))
        id = x_server_local_get_mir_id (X_SERVER_LOCAL (display_server));
    else
        id = session_get_env (session, "MIR_SERVER_NAME");

    if (id)
    {
        l_debug (seat, "Marking Mir session %s as the next session", id);
        unity_system_compositor_set_next_session (SEAT_UNITY (seat)->priv->compositor, id);
    }
    else
    {
        l_debug (seat, "Failed to work out session ID to mark");
    }

    SEAT_CLASS (seat_unity_parent_class)->set_next_session (seat, session);
}

static void
seat_unity_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    const gchar *path;
    XServerLocal *x_server;

    x_server = X_SERVER_LOCAL (display_server);
    path = x_server_local_get_authority_file_path (x_server);
    process_set_env (script, "DISPLAY", x_server_get_address (X_SERVER (x_server)));
    process_set_env (script, "XAUTHORITY", path);

    SEAT_CLASS (seat_unity_parent_class)->run_script (seat, display_server, script);
}

static void
seat_unity_stop (Seat *seat)
{
    /* Stop the compositor first */
    if (SEAT_UNITY (seat)->priv->compositor)
    {
        display_server_stop (DISPLAY_SERVER (SEAT_UNITY (seat)->priv->compositor));
        return;
    }

    SEAT_CLASS (seat_unity_parent_class)->stop (seat);
}

static void
seat_unity_init (SeatUnity *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_UNITY_TYPE, SeatUnityPrivate);
}

static void
seat_unity_finalize (GObject *object)
{
    SeatUnity *seat = SEAT_UNITY (object);

    if (seat->priv->compositor)
        g_object_unref (seat->priv->compositor);
    if (seat->priv->active_session)
        g_object_unref (seat->priv->active_session);
    if (seat->priv->active_display_server)
        g_object_unref (seat->priv->active_display_server);

    G_OBJECT_CLASS (seat_unity_parent_class)->finalize (object);
}

static void
seat_unity_class_init (SeatUnityClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SeatClass *seat_class = SEAT_CLASS (klass);

    object_class->finalize = seat_unity_finalize;
    seat_class->get_start_local_sessions = seat_unity_get_start_local_sessions;
    seat_class->setup = seat_unity_setup;
    seat_class->start = seat_unity_start;
    seat_class->create_display_server = seat_unity_create_display_server;
    seat_class->create_greeter_session = seat_unity_create_greeter_session;
    seat_class->create_session = seat_unity_create_session;
    seat_class->set_active_session = seat_unity_set_active_session;
    seat_class->get_active_session = seat_unity_get_active_session;
    seat_class->set_next_session = seat_unity_set_next_session;
    seat_class->run_script = seat_unity_run_script;
    seat_class->stop = seat_unity_stop;

    g_type_class_add_private (klass, sizeof (SeatUnityPrivate));
}
