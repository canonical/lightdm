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
#include "plymouth.h"
#include "vt.h"

G_DEFINE_TYPE (SeatXLocal, seat_xlocal, SEAT_TYPE);

static void
seat_xlocal_setup (Seat *seat)
{
    seat_set_can_switch (seat, TRUE);
    seat_set_share_display_server (seat, seat_get_boolean_property (seat, "xserver-share"));
    SEAT_CLASS (seat_xlocal_parent_class)->setup (seat);
}

static gboolean
seat_xlocal_start (Seat *seat)
{
   
    return SEAT_CLASS (seat_xlocal_parent_class)->start (seat);
}

static void
x_server_ready_cb (XServerLocal *x_server, Seat *seat)
{
    /* Quit Plymouth */
    plymouth_quit (TRUE);
}

static void
x_server_transition_plymouth_cb (XServerLocal *x_server, Seat *seat)
{
    /* Quit Plymouth if we didn't do the transition */
    if (plymouth_get_is_running ())
        plymouth_quit (FALSE);

    g_signal_handlers_disconnect_matched (x_server, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, x_server_transition_plymouth_cb, NULL);
}

static void
x_server_stopped_cb (XServerLocal *x_server, Seat *seat)
{
    gint vt;

    /* Can re-use the VT */
    vt = display_server_get_vt (DISPLAY_SERVER (x_server));
    if (vt > 0)
        vt_unref (vt);

    g_signal_handlers_disconnect_matched (x_server, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, x_server_stopped_cb, NULL);
}

static DisplayServer *
seat_xlocal_create_display_server (Seat *seat)
{
    XServerLocal *x_server;
    const gchar *command = NULL, *layout = NULL, *config_file = NULL, *xdmcp_manager = NULL, *key_name = NULL;
    gboolean allow_tcp;
    gint vt = -1, port = 0;

    if (vt > 0)
        g_debug ("Starting local X display on VT %d", vt);
    else
        g_debug ("Starting local X display");
  
    x_server = x_server_local_new ();

    /* If running inside an X server use Xephyr instead */
    if (g_getenv ("DISPLAY"))
        command = "Xephyr";
    if (!command)
        command = seat_get_string_property (seat, "xserver-command");
    if (command)
        x_server_local_set_command (x_server, command);

    /* If Plymouth is running, stop it */
    if (plymouth_get_is_active () && plymouth_has_active_vt ())
    {
        gint active_vt = vt_get_active ();
        if (active_vt >= vt_get_min ())
        {
            vt = active_vt;
            g_signal_connect (x_server, "ready", G_CALLBACK (x_server_ready_cb), seat);
            g_signal_connect (x_server, "stopped", G_CALLBACK (x_server_transition_plymouth_cb), seat);
            plymouth_deactivate ();
        }
        else
            g_debug ("Plymouth is running on VT %d, but this is less than the configured minimum of %d so not replacing it", active_vt, vt_get_min ());
    }
    if (plymouth_get_is_active ())
        plymouth_quit (FALSE);
    if (vt < 0)
        vt = vt_get_unused ();
    if (vt >= 0)
    {
        vt_ref (vt);
        x_server_local_set_vt (x_server, vt);
        g_signal_connect (x_server, "stopped", G_CALLBACK (x_server_stopped_cb), seat);
    }

    layout = seat_get_string_property (seat, "xserver-layout");
    if (layout)
        x_server_local_set_layout (x_server, layout);

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
            g_debug ("Error getting key %s", error->message);
        g_clear_error (&error);      

        if (result)
        {
            gchar *key = NULL;

            if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                key = g_key_file_get_string (keys, "keyring", key_name, NULL);
            else
                g_debug ("Key %s not defined", key_name);

            if (key)
                x_server_local_set_xdmcp_key (x_server, key);
            g_free (key);
        }

        g_free (path);
        g_key_file_free (keys);
    }

    return DISPLAY_SERVER (x_server);
}

static Greeter *
seat_xlocal_create_greeter_session (Seat *seat)
{
    Greeter *greeter_session;

    greeter_session = SEAT_CLASS (seat_xlocal_parent_class)->create_greeter_session (seat);
    session_set_env (SESSION (greeter_session), "XDG_SEAT", "seat0");

    return greeter_session;
}

static Session *
seat_xlocal_create_session (Seat *seat)
{
    Session *session;

    session = SEAT_CLASS (seat_xlocal_parent_class)->create_session (seat);
    session_set_env (SESSION (session), "XDG_SEAT", "seat0");

    return session;
}

static void
seat_xlocal_set_active_session (Seat *seat, Session *session)
{
    gint vt = display_server_get_vt (session_get_display_server (session));
    if (vt >= 0)
        vt_set_active (vt);

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
        if (display_server_get_vt (session_get_display_server (session)) == vt)
            return session;
    }

    return NULL;
}

static void
seat_xlocal_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    const gchar *path;
    XServerLocal *x_server;

    x_server = X_SERVER_LOCAL (display_server);
    path = x_server_local_get_authority_file_path (x_server);
    process_set_env (script, "DISPLAY", x_server_get_address (X_SERVER (x_server)));
    process_set_env (script, "XAUTHORITY", path);

    SEAT_CLASS (seat_xlocal_parent_class)->run_script (seat, display_server, script);
}

static void
seat_xlocal_init (SeatXLocal *seat)
{
}

static void
seat_xlocal_class_init (SeatXLocalClass *klass)
{
    SeatClass *seat_class = SEAT_CLASS (klass);

    seat_class->setup = seat_xlocal_setup;
    seat_class->start = seat_xlocal_start;
    seat_class->create_display_server = seat_xlocal_create_display_server;
    seat_class->create_greeter_session = seat_xlocal_create_greeter_session;
    seat_class->create_session = seat_xlocal_create_session;
    seat_class->set_active_session = seat_xlocal_set_active_session;
    seat_class->get_active_session = seat_xlocal_get_active_session;
    seat_class->run_script = seat_xlocal_run_script;
}
