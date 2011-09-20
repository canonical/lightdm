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
seat_xlocal_create_session (Seat *seat, Display *display)
{
    XServerLocal *xserver;
    XSession *session;
    gchar *tty;

    xserver = XSERVER_LOCAL (display_get_display_server (display));

    session = xsession_new (XSERVER (xserver));
    tty = g_strdup_printf ("/dev/tty%d", xserver_local_get_vt (xserver));
    session_set_console_kit_parameter (SESSION (session), "x11-display-device", g_variant_new_string (tty));
    g_free (tty);
    session_set_console_kit_parameter (SESSION (session), "is-local", g_variant_new_boolean (TRUE));

    return SESSION (session);
}

static void
seat_xlocal_set_active_display (Seat *seat, Display *display)
{
    gint number = xserver_local_get_vt (XSERVER_LOCAL (XSERVER (display_get_display_server (display))));
    if (number >= 0)
        vt_set_active (number);

    SEAT_CLASS (seat_xlocal_parent_class)->set_active_display (seat, display);
}

static void
seat_xlocal_run_script (Seat *seat, Display *display, Process *script)
{
    gchar *path;
    XServerLocal *xserver;

    xserver = XSERVER_LOCAL (display_get_display_server (display));
    path = xserver_local_get_authority_file_path (xserver);
    process_set_env (script, "DISPLAY", xserver_get_address (XSERVER (xserver)));
    process_set_env (script, "XAUTHORITY", path);
    g_free (path);

    SEAT_CLASS (seat_xlocal_parent_class)->run_script (seat, display, script);
}

static void
seat_xlocal_display_removed (Seat *seat, Display *display)
{
    if (seat_get_is_stopping (seat))
        return;

    /* If this is the only display and it failed to start then stop this seat */
    if (g_list_length (seat_get_displays (seat)) == 0 && !display_get_is_ready (display))
    {
        g_debug ("Stopping X local seat, failed to start a display");
        seat_stop (seat);
        return;
    }

    /* Show a new greeter */  
    if (display == seat_get_active_display (seat))
    {
        g_debug ("Active display stopped, switching to greeter");
        seat_switch_to_greeter (seat);
    }
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
    seat_class->set_active_display = seat_xlocal_set_active_display;
    seat_class->run_script = seat_xlocal_run_script;
    seat_class->display_removed = seat_xlocal_display_removed;
}
