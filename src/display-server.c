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
    /* TRUE when started */
    gboolean is_ready;

    /* TRUE when being stopped */
    gboolean stopping;

    /* TRUE when the display server has stopped */
    gboolean stopped;
};

static void display_server_logger_iface_init (LoggerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (DisplayServer, display_server, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (DisplayServer)
                         G_IMPLEMENT_INTERFACE (LOGGER_TYPE, display_server_logger_iface_init))

const gchar *
display_server_get_session_type (DisplayServer *server)
{
    return DISPLAY_SERVER_GET_CLASS (server)->get_session_type (server);
}

DisplayServer *
display_server_get_parent (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return DISPLAY_SERVER_GET_CLASS (server)->get_parent (server);
}

static DisplayServer *
display_server_real_get_parent (DisplayServer *server)
{
    return NULL;
}

gboolean
display_server_get_can_share (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, FALSE);
    return DISPLAY_SERVER_GET_CLASS (server)->get_can_share (server);
}

static gboolean
display_server_real_get_can_share (DisplayServer *server)
{
    return FALSE;
}

gint
display_server_get_vt (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, -1);
    return DISPLAY_SERVER_GET_CLASS (server)->get_vt (server);
}

static gint
display_server_real_get_vt (DisplayServer *server)
{
    return -1;
}

gboolean
display_server_start (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, FALSE);
    return DISPLAY_SERVER_GET_CLASS (server)->start (server);
}

gboolean
display_server_get_is_ready (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, FALSE);
    return server->priv->is_ready;
}

static gboolean
display_server_real_start (DisplayServer *server)
{
    server->priv->is_ready = TRUE;
    g_signal_emit (server, signals[READY], 0);
    return TRUE;
}

void
display_server_connect_session (DisplayServer *server, Session *session)
{
    return DISPLAY_SERVER_GET_CLASS (server)->connect_session (server, session);
}

static void
display_server_real_connect_session (DisplayServer *server, Session *session)
{
}

void
display_server_disconnect_session (DisplayServer *server, Session *session)
{
    return DISPLAY_SERVER_GET_CLASS (server)->disconnect_session (server, session);
}

static void
display_server_real_disconnect_session (DisplayServer *server, Session *session)
{
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
display_server_get_is_stopping (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, FALSE);
    return server->priv->stopping;
}

static void
display_server_real_stop (DisplayServer *server)
{
    g_signal_emit (server, signals[STOPPED], 0);
}

static void
display_server_init (DisplayServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, DISPLAY_SERVER_TYPE, DisplayServerPrivate);
}

static void
display_server_class_init (DisplayServerClass *klass)
{
    klass->get_parent = display_server_real_get_parent;  
    klass->get_can_share = display_server_real_get_can_share;
    klass->get_vt = display_server_real_get_vt;
    klass->start = display_server_real_start;
    klass->connect_session = display_server_real_connect_session;
    klass->disconnect_session = display_server_real_disconnect_session;
    klass->stop = display_server_real_stop;

    signals[READY] =
        g_signal_new (DISPLAY_SERVER_SIGNAL_READY,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayServerClass, ready),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
    signals[STOPPED] =
        g_signal_new (DISPLAY_SERVER_SIGNAL_STOPPED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayServerClass, stopped),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static gint
display_server_real_logprefix (Logger *self, gchar *buf, gulong buflen)
{
    return g_snprintf (buf, buflen, "DisplayServer: ");
}

static void
display_server_logger_iface_init (LoggerInterface *iface)
{
    iface->logprefix = &display_server_real_logprefix;
}
