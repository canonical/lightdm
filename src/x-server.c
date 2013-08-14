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
#include <string.h>
#include <xcb/xcb.h>

#include "x-server.h"
#include "configuration.h"

struct XServerPrivate
{  
    /* Host running the server */
    gchar *hostname;

    /* Display number */
    guint number;

    /* Cached server address */
    gchar *address;

    /* Authority */
    XAuthority *authority;

    /* Connection to this X server */
    xcb_connection_t *connection;
};

G_DEFINE_TYPE (XServer, x_server, DISPLAY_SERVER_TYPE);

void
x_server_set_hostname (XServer *server, const gchar *hostname)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->hostname);
    server->priv->hostname = g_strdup (hostname);
    g_free (server->priv->address);
    server->priv->address = NULL;
}

gchar *
x_server_get_hostname (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->hostname;
}

void
x_server_set_display_number (XServer *server, guint number)
{
    g_return_if_fail (server != NULL);
    server->priv->number = number;
    g_free (server->priv->address);
    server->priv->address = NULL;
}

guint
x_server_get_display_number (XServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->number;
}

const gchar *
x_server_get_address (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);

    if (!server->priv->address)
    {
        if (server->priv->hostname)
            server->priv->address = g_strdup_printf("%s:%d", server->priv->hostname, server->priv->number);
        else
            server->priv->address = g_strdup_printf(":%d", server->priv->number);
    }  

    return server->priv->address;
}

void
x_server_set_authority (XServer *server, XAuthority *authority)
{
    g_return_if_fail (server != NULL);

    if (server->priv->authority)
        g_object_unref (server->priv->authority);
    if (authority)
        server->priv->authority = g_object_ref (authority);
    else
        server->priv->authority = NULL;
}

XAuthority *
x_server_get_authority (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->authority;
}

static const gchar *
x_server_get_session_type (DisplayServer *server)
{
    return "x";
}

static gboolean
x_server_get_can_share (DisplayServer *server)
{
    return TRUE;
}

static gboolean
x_server_start (DisplayServer *display_server)
{
    XServer *server = X_SERVER (display_server);
    xcb_auth_info_t *auth = NULL, a;

    if (server->priv->authority)
    {
        a.namelen = strlen (x_authority_get_authorization_name (server->priv->authority));
        a.name = (char *) x_authority_get_authorization_name (server->priv->authority);
        a.datalen = x_authority_get_authorization_data_length (server->priv->authority);
        a.data = (char *) x_authority_get_authorization_data (server->priv->authority);
        auth = &a;
    }

    /* Open connection */  
    l_debug (server, "Connecting to XServer %s", x_server_get_address (server));
    server->priv->connection = xcb_connect_to_display_with_auth_info (x_server_get_address (server), auth, NULL);
    if (xcb_connection_has_error (server->priv->connection))
    {
        l_debug (server, "Error connecting to XServer %s", x_server_get_address (server));
        return FALSE;
    }

    return DISPLAY_SERVER_CLASS (x_server_parent_class)->start (display_server);
}

static void
x_server_connect_session (DisplayServer *display_server, Session *session)
{
    gint vt;

    display_server = session_get_display_server (session);

    vt = display_server_get_vt (display_server);
    if (vt > 0)
    {
        gchar *t;

        t = g_strdup_printf ("/dev/tty%d", vt);
        session_set_tty (session, t);
        g_free (t);

        t = g_strdup_printf ("%d", vt);
        session_set_env (session, "XDG_VTNR", t);
        g_free (t);
    }

    session_set_env (session, "DISPLAY", x_server_get_address (X_SERVER (display_server)));
    session_set_tty (session, x_server_get_address (X_SERVER (display_server)));
    session_set_xdisplay (session, x_server_get_address (X_SERVER (display_server)));
    session_set_remote_host_name (session, x_server_get_hostname (X_SERVER (display_server)));
    session_set_x_authority (session,
                             x_server_get_authority (X_SERVER (display_server)),
                             config_get_boolean (config_get_instance (), "LightDM", "user-authority-in-system-dir"));
}

static void
x_server_disconnect_session (DisplayServer *display_server, Session *session)
{
    gint vt;

    vt = display_server_get_vt (display_server);
    if (vt > 0)
    {
        session_set_tty (session, NULL);
        session_unset_env (session, "XDG_VTNR");
    }
    session_unset_env (session, "DISPLAY");
    session_set_xdisplay (session, NULL);
    session_set_remote_host_name (session, NULL);
    session_set_x_authority (session, NULL, FALSE);
}

void
x_server_init (XServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, X_SERVER_TYPE, XServerPrivate);
}

static void
x_server_finalize (GObject *object)
{
    XServer *self;

    self = X_SERVER (object);

    g_free (self->priv->hostname);
    g_free (self->priv->address);
    if (self->priv->authority)
        g_object_unref (self->priv->authority);
    if (self->priv->connection)
        xcb_disconnect (self->priv->connection);

    G_OBJECT_CLASS (x_server_parent_class)->finalize (object);
}

static void
x_server_class_init (XServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->get_session_type = x_server_get_session_type;    
    display_server_class->get_can_share = x_server_get_can_share;
    display_server_class->start = x_server_start;
    display_server_class->connect_session = x_server_connect_session;
    display_server_class->disconnect_session = x_server_disconnect_session;
    object_class->finalize = x_server_finalize;

    g_type_class_add_private (klass, sizeof (XServerPrivate));
}
