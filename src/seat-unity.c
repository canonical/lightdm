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
#include "x-server-xmir.h"
#include "vt.h"
#include "plymouth.h"

typedef struct
{
    /* System compositor */
    UnitySystemCompositor *compositor;

    /* X server being used for XDMCP */
    XServerXmir *xdmcp_x_server;

    /* Next Mir ID to use for a Xmir servers */
    gint next_x_server_id;

    /* The currently visible session */
    Session *active_session;
    DisplayServer *active_display_server;
} SeatUnityPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SeatUnity, seat_unity, SEAT_TYPE)

static XServerXmir *create_x_server (Seat *seat);

static void
seat_unity_setup (Seat *seat)
{
    seat_set_supports_multi_session (seat, TRUE);
    SEAT_CLASS (seat_unity_parent_class)->setup (seat);
}

static void
check_stopped (SeatUnity *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (seat);
    if (!priv->compositor && !priv->xdmcp_x_server)
        SEAT_CLASS (seat_unity_parent_class)->stop (SEAT (seat));
}

static void
xdmcp_x_server_stopped_cb (DisplayServer *display_server, Seat *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

    l_debug (seat, "XDMCP X server stopped");

    g_signal_handlers_disconnect_matched (priv->xdmcp_x_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    priv->xdmcp_x_server = NULL;

    g_object_unref (display_server);

    if (seat_get_is_stopping (seat))
        check_stopped (SEAT_UNITY (seat));
    else
        seat_stop (seat);
}

static void
compositor_ready_cb (UnitySystemCompositor *compositor, SeatUnity *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (seat);

    l_debug (seat, "Compositor ready");

    /* If running as an XDMCP client then just start an X server */
    const gchar *xdmcp_manager = seat_get_string_property (SEAT (seat), "xdmcp-manager");
    if (xdmcp_manager)
    {
        priv->xdmcp_x_server = create_x_server (SEAT (seat));
        x_server_local_set_xdmcp_server (X_SERVER_LOCAL (priv->xdmcp_x_server), xdmcp_manager);
        gint port = seat_get_integer_property (SEAT (seat), "xdmcp-port");
        if (port > 0)
            x_server_local_set_xdmcp_port (X_SERVER_LOCAL (priv->xdmcp_x_server), port);
        const gchar *key_name = seat_get_string_property (SEAT (seat), "xdmcp-key");
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
                if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                {
                    g_autofree gchar *key = g_key_file_get_string (keys, "keyring", key_name, NULL);
                    if (key)
                        x_server_local_set_xdmcp_key (X_SERVER_LOCAL (priv->xdmcp_x_server), key);
                }
                else
                    l_debug (seat, "Key %s not defined", key_name);
            }
        }

        g_signal_connect (priv->xdmcp_x_server, DISPLAY_SERVER_SIGNAL_STOPPED, G_CALLBACK (xdmcp_x_server_stopped_cb), seat);
        if (!display_server_start (DISPLAY_SERVER (priv->xdmcp_x_server)))
            seat_stop (SEAT (seat));
    }

    SEAT_CLASS (seat_unity_parent_class)->start (SEAT (seat));
}

static void
compositor_stopped_cb (UnitySystemCompositor *compositor, SeatUnity *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (seat);

    l_debug (seat, "Compositor stopped");

    g_clear_object (&priv->compositor);

    if (seat_get_is_stopping (SEAT (seat)))
        check_stopped (seat);
    else
        seat_stop (SEAT (seat));
}

static gboolean
seat_unity_start (Seat *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

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

    int timeout = seat_get_integer_property (SEAT (seat), "unity-compositor-timeout");
    if (timeout <= 0)
        timeout = 60;

    priv->compositor = unity_system_compositor_new ();
    g_signal_connect (priv->compositor, DISPLAY_SERVER_SIGNAL_READY, G_CALLBACK (compositor_ready_cb), seat);
    g_signal_connect (priv->compositor, DISPLAY_SERVER_SIGNAL_STOPPED, G_CALLBACK (compositor_stopped_cb), seat);
    unity_system_compositor_set_command (priv->compositor, seat_get_string_property (SEAT (seat), "unity-compositor-command"));
    unity_system_compositor_set_vt (priv->compositor, vt);
    unity_system_compositor_set_timeout (priv->compositor, timeout);

    return display_server_start (DISPLAY_SERVER (priv->compositor));
}

static XServerXmir *
create_x_server (Seat *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

    l_debug (seat, "Starting X server on Unity compositor");

    g_autoptr(XServerXmir) x_server = x_server_xmir_new (priv->compositor);

    const gchar *command = seat_get_string_property (seat, "xmir-command");
    x_server_local_set_command (X_SERVER_LOCAL (x_server), command);

    g_autofree gchar *id = g_strdup_printf ("x-%d", priv->next_x_server_id);
    priv->next_x_server_id++;
    x_server_xmir_set_mir_id (x_server, id);
    x_server_xmir_set_mir_socket (x_server, unity_system_compositor_get_socket (priv->compositor));

    g_autofree gchar *number = g_strdup_printf ("%d", x_server_get_display_number (X_SERVER (x_server)));
    g_autoptr(XAuthority) cookie = x_authority_new_local_cookie (number);
    x_server_set_authority (X_SERVER (x_server), cookie);

    const gchar *layout = seat_get_string_property (seat, "xserver-layout");
    if (layout)
        x_server_local_set_layout (X_SERVER_LOCAL (x_server), layout);

    x_server_local_set_xdg_seat (X_SERVER_LOCAL (x_server), seat_get_name (seat));

    const gchar *config_file = seat_get_string_property (seat, "xserver-config");
    if (config_file)
        x_server_local_set_config (X_SERVER_LOCAL (x_server), config_file);

    gboolean allow_tcp = seat_get_boolean_property (seat, "xserver-allow-tcp");
    x_server_local_set_allow_tcp (X_SERVER_LOCAL (x_server), allow_tcp);

    return g_steal_pointer (&x_server);
}

static DisplayServer *
seat_unity_create_display_server (Seat *seat, Session *session)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

    const gchar *session_type = session_get_session_type (session);
    if (strcmp (session_type, "x") == 0)
        return DISPLAY_SERVER (create_x_server (seat));
    else if (strcmp (session_type, "mir") == 0)
        return g_object_ref (DISPLAY_SERVER (priv->compositor));
    else
    {
        l_warning (seat, "Can't create unsupported display server '%s'", session_type);
        return NULL;
    }
}

static gboolean
seat_unity_display_server_is_used (Seat *seat, DisplayServer *display_server)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

    if (display_server == DISPLAY_SERVER (priv->compositor))
        return TRUE;

    return SEAT_CLASS (seat_unity_parent_class)->display_server_is_used (seat, display_server);
}

static GreeterSession *
seat_unity_create_greeter_session (Seat *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

    GreeterSession *greeter_session = SEAT_CLASS (seat_unity_parent_class)->create_greeter_session (seat);
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
seat_unity_create_session (Seat *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

    Session *session = SEAT_CLASS (seat_unity_parent_class)->create_session (seat);
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
    if (IS_UNITY_SYSTEM_COMPOSITOR (display_server))
        return session_get_env (session, "MIR_SERVER_NAME");
    if (IS_X_SERVER_XMIR (display_server))
        return x_server_xmir_get_mir_id (X_SERVER_XMIR (display_server));

    return NULL;
}

static void
seat_unity_set_active_session (Seat *seat, Session *session)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

    const gchar *old_id = get_mir_id (priv->active_session);
    const gchar *new_id = get_mir_id (session);

    g_clear_object (&priv->active_session);
    priv->active_session = g_object_ref (session);

    if (g_strcmp0 (old_id, new_id) != 0)
        unity_system_compositor_set_active_session (priv->compositor, new_id);

    SEAT_CLASS (seat_unity_parent_class)->set_active_session (seat, session);
}

static Session *
seat_unity_get_active_session (Seat *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));
    return priv->active_session;
}

static void
seat_unity_set_next_session (Seat *seat, Session *session)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

    if (!session)
        return;

    DisplayServer *display_server = session_get_display_server (session);

    const gchar *id = NULL;
    if (IS_X_SERVER_LOCAL (display_server))
        id = x_server_xmir_get_mir_id (X_SERVER_XMIR (display_server));
    else
        id = session_get_env (session, "MIR_SERVER_NAME");

    if (id)
    {
        l_debug (seat, "Marking Mir session %s as the next session", id);
        unity_system_compositor_set_next_session (priv->compositor, id);
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
    if (IS_X_SERVER_XMIR (display_server))
    {
        XServerXmir *x_server = X_SERVER_XMIR (display_server);
        const gchar *path = x_server_local_get_authority_file_path (X_SERVER_LOCAL (x_server));
        process_set_env (script, "DISPLAY", x_server_get_address (X_SERVER (x_server)));
        process_set_env (script, "XAUTHORITY", path);
    }

    SEAT_CLASS (seat_unity_parent_class)->run_script (seat, display_server, script);
}

static void
seat_unity_stop (Seat *seat)
{
    SeatUnityPrivate *priv = seat_unity_get_instance_private (SEAT_UNITY (seat));

    /* Stop the compositor first */
    if (priv->compositor)
        display_server_stop (DISPLAY_SERVER (priv->compositor));

    /* Stop the XDMCP X server first */
    if (priv->xdmcp_x_server)
        display_server_stop (DISPLAY_SERVER (priv->xdmcp_x_server));

    check_stopped (SEAT_UNITY (seat));
}

static void
seat_unity_init (SeatUnity *seat)
{
}

static void
seat_unity_finalize (GObject *object)
{
    SeatUnity *seat = SEAT_UNITY (object);
    SeatUnityPrivate *priv = seat_unity_get_instance_private (seat);

    g_clear_object (&priv->compositor);
    if (priv->xdmcp_x_server)
    {
        g_signal_handlers_disconnect_matched (priv->xdmcp_x_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
        g_object_unref (priv->xdmcp_x_server);
    }
    g_clear_object (&priv->active_session);
    g_clear_object (&priv->active_display_server);

    G_OBJECT_CLASS (seat_unity_parent_class)->finalize (object);
}

static void
seat_unity_class_init (SeatUnityClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SeatClass *seat_class = SEAT_CLASS (klass);

    object_class->finalize = seat_unity_finalize;
    seat_class->setup = seat_unity_setup;
    seat_class->start = seat_unity_start;
    seat_class->create_display_server = seat_unity_create_display_server;
    seat_class->display_server_is_used = seat_unity_display_server_is_used;
    seat_class->create_greeter_session = seat_unity_create_greeter_session;
    seat_class->create_session = seat_unity_create_session;
    seat_class->set_active_session = seat_unity_set_active_session;
    seat_class->get_active_session = seat_unity_get_active_session;
    seat_class->set_next_session = seat_unity_set_next_session;
    seat_class->run_script = seat_unity_run_script;
    seat_class->stop = seat_unity_stop;
}
