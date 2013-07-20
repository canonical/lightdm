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

struct DisplayServerPrivate
{
    /* Unique name for this display server */
    gchar *name;

    /* TRUE if sessions should be automatically started on this display server */
    gboolean start_local_sessions;

    /* TRUE when being stopped */
    gboolean stopping;

    /* TRUE when the display server has stopped */
    gboolean stopped;
};

G_DEFINE_TYPE (DisplayServer, display_server, G_TYPE_OBJECT);

void
display_server_set_name (DisplayServer *server, const gchar *name)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->name);
    server->priv->name = g_strdup (name);
}

const gchar *
display_server_get_name (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->name;
}

void
display_server_set_start_local_sessions (DisplayServer *server, gboolean start_local_sessions)
{
    g_return_if_fail (server != NULL);
    server->priv->start_local_sessions = start_local_sessions;
}

gboolean
display_server_get_start_local_sessions (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, FALSE);
    return server->priv->start_local_sessions;
}

gboolean
display_server_start (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, FALSE);
    return DISPLAY_SERVER_GET_CLASS (server)->start (server);
}

static gboolean
display_server_real_start (DisplayServer *server)
{
    g_signal_emit (server, signals[READY], 0);
    return TRUE;
}

void
display_server_stop (DisplayServer *server)
{
    g_return_if_fail (server != NULL);

    if (server->priv->stopping)
        return;
    server->priv->stopping = TRUE;

    DISPLAY_SERVER_GET_CLASS (server)->stop (server);
}

gboolean
display_server_get_is_stopped (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, TRUE);
    return server->priv->stopped;
}

static void
display_server_real_stop (DisplayServer *server)
{
    server->priv->stopped = TRUE;
    g_signal_emit (server, signals[STOPPED], 0);
}

static gboolean
display_server_real_get_is_stopped (DisplayServer *server)
{
    return server->priv->stopped;
}

static void
display_server_init (DisplayServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, DISPLAY_SERVER_TYPE, DisplayServerPrivate);
    server->priv->start_local_sessions = TRUE;
}

static void
display_server_class_init (DisplayServerClass *klass)
{
    klass->start = display_server_real_start;
    klass->stop = display_server_real_stop;

    g_type_class_add_private (klass, sizeof (DisplayServerPrivate));

    signals[READY] =
        g_signal_new ("ready",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayServerClass, ready),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayServerClass, stopped),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}
