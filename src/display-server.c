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

#include <config.h>

#include "display-server.h"

enum {
    READY,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (DisplayServer, display_server, G_TYPE_OBJECT);

static gboolean
display_server_real_start (DisplayServer *server)
{
    g_signal_emit (server, signals[READY], 0);
    return TRUE;
}

gboolean
display_server_start (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, FALSE);
    return DISPLAY_SERVER_GET_CLASS (server)->start (server);
}

static void
display_server_real_stop (DisplayServer *server)
{
    g_signal_emit (server, signals[STOPPED], 0);
}

void
display_server_stop (DisplayServer *server)
{
    g_return_if_fail (server != NULL);
    DISPLAY_SERVER_GET_CLASS (server)->stop (server);
}

static void
display_server_init (DisplayServer *server)
{
}

static void
display_server_class_init (DisplayServerClass *klass)
{
    klass->start = display_server_real_start;
    klass->stop = display_server_real_stop;

    signals[READY] =
        g_signal_new ("ready",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayServerClass, ready),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayServerClass, stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
