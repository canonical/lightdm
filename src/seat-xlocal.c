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
#include "xdisplay.h"
#include "xserver-local.h"
#include "vt.h"

G_DEFINE_TYPE (SeatXLocal, seat_xlocal, SEAT_TYPE);

static void
seat_xlocal_setup (Seat *seat)
{
    seat_set_can_switch (seat, TRUE);
    SEAT_CLASS (seat_xlocal_parent_class)->setup (seat);
}

static Display *
seat_xlocal_add_display (Seat *seat)
{
    XServerLocal *xserver;
    XDisplay *display;
    const gchar *config_section;
    gchar *command = NULL, *layout = NULL, *config_file = NULL, *xdmcp_manager = NULL, *key_name = NULL, *key = NULL;
    gint port = 0;

    g_debug ("Starting Local X Display");
  
    xserver = xserver_local_new ();

    config_section = seat_get_config_section (seat);

    /* If running inside an X server use Xephyr instead */
    if (g_getenv ("DISPLAY"))
        command = g_strdup ("Xephyr");
    if (!command && config_section)
        command = config_get_string (config_get_instance (), config_section, "xserver-command");
    if (!command)
        command = config_get_string (config_get_instance (), "SeatDefaults", "xserver-command");
    if (command)
        xserver_local_set_command (xserver, command);
    g_free (command);

    if (config_section)
        layout = config_get_string (config_get_instance (), config_section, "xserver-layout");
    if (!layout)
        layout = config_get_string (config_get_instance (), "SeatDefaults", "layout");
    if (layout)
        xserver_local_set_layout (xserver, layout);
    g_free (layout);

    if (config_section)
        config_file = config_get_string (config_get_instance (), config_section, "xserver-config");
    if (!config_file)
        config_file = config_get_string (config_get_instance (), "SeatDefaults", "xserver-config");
    if (config_file)
        xserver_local_set_config (xserver, config_file);
    g_free (config_file);

    if (config_section)
        xdmcp_manager = config_get_string (config_get_instance (), config_section, "xdmcp-manager");
    if (!xdmcp_manager)
        xdmcp_manager = config_get_string (config_get_instance (), "SeatDefaults", "xdmcp-manager");
    if (xdmcp_manager)
        xserver_local_set_xdmcp_server (xserver, xdmcp_manager);
    g_free (xdmcp_manager);

    if (config_section && config_has_key (config_get_instance (), config_section, "xdmcp-port"))
        port = config_get_integer (config_get_instance (), config_section, "xdmcp-port");
    else if (config_has_key (config_get_instance (), "SeatDefaults", "xdmcp-port"))
        port = config_get_integer (config_get_instance (), "SeatDefaults", "xdmcp-port");
    if (port > 0)
        xserver_local_set_xdmcp_port (xserver, port);

    if (config_section)
        key_name = config_get_string (config_get_instance (), config_section, "xdmcp-key");
    if (!key_name)
        key_name = config_get_string (config_get_instance (), "SeatDefaults", "xdmcp-key");
    if (key_name)
    {
        GKeyFile *keys;
        GError *error = NULL;

        keys = g_key_file_new ();
        if (g_key_file_load_from_file (keys, KEY_FILE, G_KEY_FILE_NONE, &error))
        {
            if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                key = g_key_file_get_string (keys, "keyring", key_name, NULL);
            else
                g_debug ("Key %s not defined", error->message);
        }
        else
            g_debug ("Error getting key %s", error->message);
        g_clear_error (&error);
        g_key_file_free (keys);
    }
    if (key)
        xserver_local_set_xdmcp_key (xserver, key);
    g_free (key_name);
    g_free (key);

    display = xdisplay_new (XSERVER (xserver));
    g_object_unref (xserver);

    return DISPLAY (display);
}

static void
seat_xlocal_set_active_display (Seat *seat, Display *display)
{
    gint number = xserver_local_get_vt (XSERVER_LOCAL (XSERVER (display_get_display_server (display))));
    if (number >= 0)
        vt_set_active (number);
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
    seat_class->add_display = seat_xlocal_add_display;
    seat_class->set_active_display = seat_xlocal_set_active_display;
}
