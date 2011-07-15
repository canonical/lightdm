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
#include "xserver.h"
#include "vt.h"

struct SeatLocalPrivate
{
    /* The section in the config for this seat */
    gchar *config_section;
};

G_DEFINE_TYPE (SeatLocal, seat_local, SEAT_TYPE);

SeatLocal *
seat_local_new (const gchar *config_section)
{
    SeatLocal *seat;
    gchar *username;
    guint timeout;

    seat = g_object_new (SEAT_LOCAL_TYPE, NULL);
    seat->priv->config_section = g_strdup (config_section);

    seat_set_can_switch (SEAT (seat), TRUE);
    username = config_get_string (config_get_instance (), config_section, "default-user");
    timeout = config_get_integer (config_get_instance (), config_section, "default-user-timeout");
    if (timeout < 0)
        timeout = 0;
    if (username)
        seat_set_autologin_user (SEAT (seat), username, timeout);

    return seat;
}

static Display *
seat_local_add_display (Seat *seat)
{
    XServer *xserver;
    XAuthorization *authorization = NULL;
    gchar *xserver_section = NULL;
    gchar *command, *dir, *filename, *path;
    gchar *number;
    gchar hostname[1024];
    Display *display;

    g_debug ("Starting display");

    xserver = xserver_new (XSERVER_TYPE_LOCAL, NULL, xserver_get_free_display_number ());
    number = g_strdup_printf ("%d", xserver_get_display_number (xserver));
    gethostname (hostname, 1024);
    authorization = xauth_new_cookie (XAUTH_FAMILY_LOCAL, hostname, number);
    g_free (number);

    xserver_set_vt (xserver, vt_get_unused ());

    command = config_get_string (config_get_instance (), "LightDM", "default-xserver-command");
    xserver_set_command (xserver, command);
    g_free (command);

    xserver_set_authorization (xserver, authorization);
    g_object_unref (authorization);

    filename = g_strdup_printf ("%s.log", xserver_get_address (xserver));
    dir = config_get_string (config_get_instance (), "directories", "log-directory");
    path = g_build_filename (dir, filename, NULL);
    g_debug ("Logging to %s", path);
    child_process_set_log_file (CHILD_PROCESS (xserver), path);
    g_free (filename);
    g_free (dir);
    g_free (path);

    /* Get the X server configuration */
    if (SEAT_LOCAL (seat)->priv->config_section)
        xserver_section = config_get_string (config_get_instance (), SEAT_LOCAL (seat)->priv->config_section, "xserver");
    if (!xserver_section)
        xserver_section = config_get_string (config_get_instance (), "LightDM", "xserver");

    if (xserver_section)
    {
        gchar *xserver_command, *xserver_layout, *xserver_config_file;

        g_debug ("Using X server configuration '%s' for display '%s'", xserver_section, SEAT_LOCAL (seat)->priv->config_section ? SEAT_LOCAL (seat)->priv->config_section : "<anonymous>");

        xserver_command = config_get_string (config_get_instance (), xserver_section, "command");
        if (xserver_command)
            xserver_set_command (xserver, xserver_command);
        g_free (xserver_command);

        xserver_layout = config_get_string (config_get_instance (), xserver_section, "layout");
        if (xserver_layout)
            xserver_set_layout (xserver, xserver_layout);
        g_free (xserver_layout);

        xserver_config_file = config_get_string (config_get_instance (), xserver_section, "config-file");
        if (xserver_config_file)
            xserver_set_config_file (xserver, xserver_config_file);
        g_free (xserver_config_file);

        g_free (xserver_section);
    }

    if (config_get_boolean (config_get_instance (), "LightDM", "use-xephyr"))
        xserver_set_command (xserver, "Xephyr");

    display = display_new (xserver);
    g_object_unref (xserver);

    return display;
}

static void
seat_local_set_active_display (Seat *seat, Display *display)
{
    gint number = xserver_get_vt (display_get_xserver (display));
    if (number >= 0)
        vt_set_active (number);
}

static void
seat_local_init (SeatLocal *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_LOCAL_TYPE, SeatLocalPrivate);
}

static void
seat_local_finalize (GObject *object)
{
    SeatLocal *self;

    self = SEAT_LOCAL (object);

    g_free (self->priv->config_section);

    G_OBJECT_CLASS (seat_local_parent_class)->finalize (object);
}

static void
seat_local_class_init (SeatLocalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SeatClass *seat_class = SEAT_CLASS (klass);

    seat_class->add_display = seat_local_add_display;
    seat_class->set_active_display = seat_local_set_active_display;
    object_class->finalize = seat_local_finalize;

    g_type_class_add_private (klass, sizeof (SeatLocalPrivate));
}
