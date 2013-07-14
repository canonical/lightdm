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
#include "xserver-local.h"
#include "xsession.h"
#include "vt.h"

G_DEFINE_TYPE (SeatXLocal, seat_xlocal, SEAT_TYPE);

static void
seat_xlocal_setup (Seat *seat)
{
    seat_set_can_switch (seat, TRUE);
    seat_set_share_display_server (seat, seat_get_boolean_property (seat, "xserver-share"));
    SEAT_CLASS (seat_xlocal_parent_class)->setup (seat);
}

static DisplayServer *
seat_xlocal_create_display_server (Seat *seat)
{
    XServerLocal *xserver;
    const gchar *command = NULL, *layout = NULL, *config_file = NULL, *xdmcp_manager = NULL, *key_name = NULL;
    gboolean allow_tcp;
    gint port = 0;

    g_debug ("Starting local X display");
  
    xserver = xserver_local_new ();

    /* If running inside an X server use Xephyr instead */
    if (g_getenv ("DISPLAY"))
        command = "Xephyr";
    if (!command)
        command = seat_get_string_property (seat, "xserver-command");
    if (command)
        xserver_local_set_command (xserver, command);

    layout = seat_get_string_property (seat, "xserver-layout");
    if (layout)
        xserver_local_set_layout (xserver, layout);

    config_file = seat_get_string_property (seat, "xserver-config");
    if (config_file)
        xserver_local_set_config (xserver, config_file);
  
    allow_tcp = seat_get_boolean_property (seat, "xserver-allow-tcp");
    xserver_local_set_allow_tcp (xserver, allow_tcp);    

    xdmcp_manager = seat_get_string_property (seat, "xdmcp-manager");
    if (xdmcp_manager)
        xserver_local_set_xdmcp_server (xserver, xdmcp_manager);

    port = seat_get_integer_property (seat, "xdmcp-port");
    if (port > 0)
        xserver_local_set_xdmcp_port (xserver, port);

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
                xserver_local_set_xdmcp_key (xserver, key);
            g_free (key);
        }

        g_free (path);
        g_key_file_free (keys);
    }

    return DISPLAY_SERVER (xserver);
}

static Session *
seat_xlocal_create_session (Seat *seat)
{
    XSession *session;

    session = xsession_new ();
    session_set_env (SESSION (session), "XDG_SEAT", "seat0");

    return SESSION (session);
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
    XServerLocal *xserver;

    xserver = XSERVER_LOCAL (display_server);
    path = xserver_local_get_authority_file_path (xserver);
    process_set_env (script, "DISPLAY", xserver_get_address (XSERVER (xserver)));
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
    seat_class->create_display_server = seat_xlocal_create_display_server;
    seat_class->create_session = seat_xlocal_create_session;
    seat_class->set_active_session = seat_xlocal_set_active_session;
    seat_class->get_active_session = seat_xlocal_get_active_session;
    seat_class->run_script = seat_xlocal_run_script;
}
