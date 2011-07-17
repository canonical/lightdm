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
#include "xserver-local.h"
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

    seat = g_object_new (SEAT_LOCAL_TYPE, NULL);
    seat->priv->config_section = g_strdup (config_section);
    seat_load_config (SEAT (seat), config_section);
    seat_set_can_switch (SEAT (seat), TRUE);

    return seat;
}

static Display *
seat_local_add_display (Seat *seat)
{
    XServerLocal *xserver;
    XAuthorization *authorization = NULL;
    gchar *number;
    gchar hostname[1024];
    Display *display;

    g_debug ("Starting display");

    xserver = xserver_local_new (SEAT_LOCAL (seat)->priv->config_section);
    number = g_strdup_printf ("%d", xserver_get_display_number (XSERVER (xserver)));
    gethostname (hostname, 1024);
    authorization = xauth_new_cookie (XAUTH_FAMILY_LOCAL, hostname, number);
    g_free (number);

    xserver_set_authorization (XSERVER (xserver), authorization);
    g_object_unref (authorization);

    display = display_new (SEAT_LOCAL (seat)->priv->config_section, DISPLAY_SERVER (xserver));
    g_object_unref (xserver);

    return display;
}

static void
seat_local_set_active_display (Seat *seat, Display *display)
{
    gint number = xserver_local_get_vt (XSERVER_LOCAL (XSERVER (display_get_display_server (display))));
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
