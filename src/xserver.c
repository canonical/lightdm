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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <xcb/xcb.h>
#include <fcntl.h>
#include <glib/gstdio.h>

#include "xserver.h"

enum {
    READY,  
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

// FIXME: Make a base class and a LocalXServer, RemoteXServer etc

struct XServerPrivate
{  
    /* Type of server */
    XServerType type;

    /* Path of file to log to */
    gchar *log_file;

    /* Command to run the X server */
    gchar *command;

    /* Config file to use */
    gchar *config_file;

    /* Server layout to use */
    gchar *layout;

    /* TRUE if the xserver has started */
    gboolean ready;

    /* Name of remote host or XDMCP manager */
    gchar *hostname;

    /* UDP/IP port to connect to XDMCP manager */
    guint port;

    /* Auhentication scheme to use */
    gchar *authentication_name;

    /* Auhentication data */  
    guchar *authentication_data;
    gsize authentication_data_length;

    /* Authorization */
    XAuthorization *authorization;
    gchar *authorization_path;
    GFile *authorization_file;

    /* VT to run on */
    gint vt;

    /* TRUE to disable setting the root window */
    gboolean no_root;

    /* Display number */
    gint display_number;

    /* Cached server address */
    gchar *address;

    /* Connection to X server */
    xcb_connection_t *connection;
};

G_DEFINE_TYPE (XServer, xserver, CHILD_PROCESS_TYPE);

static GHashTable *servers = NULL;

XServer *
xserver_new (XServerType type, const gchar *hostname, gint display_number)
{
    XServer *self = g_object_new (XSERVER_TYPE, NULL);

    self->priv->type = type;
    self->priv->hostname = g_strdup (hostname);
    self->priv->display_number = display_number;
  
    return self;
}

XServerType
xserver_get_server_type (XServer *server)
{
    return server->priv->type;
}

void
xserver_set_command (XServer *server, const gchar *command)
{
    g_free (server->priv->command);
    server->priv->command = g_strdup (command);
}

void
xserver_set_config_file (XServer *server, const gchar *config_file)
{
    g_free (server->priv->config_file);
    server->priv->config_file = g_strdup (config_file);
}

const gchar *
xserver_get_config_file (XServer *server)
{
    return server->priv->config_file;
}

void
xserver_set_layout (XServer *server, const gchar *layout)
{
    g_free (server->priv->layout);
    server->priv->layout = g_strdup (layout);
}

const gchar *
xserver_get_layout (XServer *server)
{
    return server->priv->layout;
}

const gchar *
xserver_get_command (XServer *server)
{
    return server->priv->command;
}

void
xserver_set_log_file (XServer *server, const gchar *log_file)
{
    g_free (server->priv->log_file);
    server->priv->log_file = g_strdup (log_file);
}

const gchar *
xserver_get_log_file (XServer *server)
{
    return server->priv->log_file;
}
  
void
xserver_set_port (XServer *server, guint port)
{
    server->priv->port = port;
}

guint xserver_get_port (XServer *server)
{
    return server->priv->port;
}

const gchar *
xserver_get_hostname (XServer *server)
{
    return server->priv->hostname;
}

gint
xserver_get_display_number (XServer *server)
{
    return server->priv->display_number;
}

const gchar *
xserver_get_address (XServer *server)
{
    if (!server->priv->address)
    {
        if (server->priv->type == XSERVER_TYPE_REMOTE)
            server->priv->address = g_strdup_printf("%s:%d", server->priv->hostname, server->priv->display_number);
        else
            server->priv->address = g_strdup_printf(":%d", server->priv->display_number);
    }  

    return server->priv->address;
}

void
xserver_set_authentication (XServer *server, const gchar *name, const guchar *data, gsize data_length)
{
    g_free (server->priv->authentication_name);
    server->priv->authentication_name = g_strdup (name);
    g_free (server->priv->authentication_data);
    server->priv->authentication_data = g_malloc (data_length);
    server->priv->authentication_data_length = data_length;
    memcpy (server->priv->authentication_data, data, data_length);
}

const gchar *
xserver_get_authentication_name (XServer *server)
{
    return server->priv->authentication_name;
}

const guchar *
xserver_get_authentication_data (XServer *server)
{
    return server->priv->authentication_data;
}

gsize
xserver_get_authentication_data_length (XServer *server)
{
    return server->priv->authentication_data_length;
}

void
xserver_set_authorization (XServer *server, XAuthorization *authorization, const gchar *path)
{
    if (server->priv->authorization)
        g_object_unref (server->priv->authorization);
    server->priv->authorization = g_object_ref (authorization);
    if (path)
        server->priv->authorization_path = g_strdup (path);
  
    /* If already running then change authorization immediately */
    if (server->priv->authorization_file)
    {
        GError *error = NULL;

        g_object_unref (server->priv->authorization_file);
        server->priv->authorization_file = xauth_write (server->priv->authorization, NULL, server->priv->authorization_path, &error);
        if (!server->priv->authorization_file)
            g_warning ("Failed to write authorization: %s", error->message);
        g_clear_error (&error);
    }
}

XAuthorization *
xserver_get_authorization (XServer *server)
{
    return server->priv->authorization;
}

void
xserver_set_vt (XServer *xserver, gint vt)
{
    xserver->priv->vt = vt;  
}

gint
xserver_get_vt (XServer *xserver)
{
    return xserver->priv->vt;
}

void
xserver_set_no_root (XServer *xserver, gboolean no_root)
{
    xserver->priv->no_root = no_root;
}

static gboolean
xserver_connect (XServer *server)
{
    gchar *xauthority = NULL;

    /* Write the authorization file */
    if (server->priv->authorization)
    {
        GError *error = NULL;

        server->priv->authorization_file = xauth_write (server->priv->authorization, NULL, server->priv->authorization_path, &error);
        if (!server->priv->authorization_file)
            g_warning ("Failed to write authorization: %s", error->message);
        g_clear_error (&error);
    }

    /* NOTE: We have to do this hack as xcb_connect_to_display_with_auth_info can't be used
     * for XDM-AUTHORIZATION-1 and the authorization data requires to know the source port */
    if (server->priv->authorization_file)
    {
        xauthority = g_strdup (getenv ("XAUTHORITY"));
        setenv ("XAUTHORITY", server->priv->authorization_path, TRUE);
    }

    g_debug ("Connecting to XServer %s", xserver_get_address (server));
    server->priv->connection = xcb_connect (xserver_get_address (server), NULL);
    if (xcb_connection_has_error (server->priv->connection))
        g_debug ("Error connecting to XServer %s", xserver_get_address (server));

    if (server->priv->authorization_file)
    {
        setenv ("XAUTHORITY", xauthority, TRUE);
        g_free (xauthority);
    }

    return xcb_connection_has_error (server->priv->connection) == 0;
}

gboolean
xserver_start (XServer *server)
{
    GError *error = NULL;
    gboolean result;
    GString *command;

    //g_return_val_if_fail (server->priv->pid == 0, FALSE);
    g_return_val_if_fail (server->priv->command != NULL, FALSE);
 
    /* Check if we can connect to the remote server */
    if (server->priv->type == XSERVER_TYPE_REMOTE)
    {
        if (!xserver_connect (server))
            return FALSE;

        server->priv->ready = TRUE;
        g_signal_emit (server, signals[READY], 0);
        return TRUE;
    }

    /* Write the authorization file */
    if (server->priv->authorization)
    {
        GError *error = NULL;

        server->priv->authorization_file = xauth_write (server->priv->authorization, NULL, server->priv->authorization_path, &error);
        if (!server->priv->authorization_file)
            g_warning ("Failed to write authorization: %s", error->message);
        g_clear_error (&error);
    }

    command = g_string_new (server->priv->command);
    g_string_append_printf (command, " :%d", server->priv->display_number);
    //g_string_append_printf (command, " vt%d");

    if (server->priv->config_file)
        g_string_append_printf (command, " -config %s", server->priv->config_file);

    if (server->priv->layout)
        g_string_append_printf (command, " -layout %s", server->priv->layout);

    if (server->priv->authorization)
        g_string_append_printf (command, " -auth %s", server->priv->authorization_path);

    if (server->priv->type == XSERVER_TYPE_LOCAL_TERMINAL)
    {
        if (server->priv->port != 0)
            g_string_append_printf (command, " -port %d", server->priv->port);
        g_string_append_printf (command, " -query %s", server->priv->hostname);
        if (strcmp (server->priv->authentication_name, "XDM-AUTHENTICATION-1") == 0 && server->priv->authentication_data_length > 0)
        {
            GString *cookie;
            gsize i;

            cookie = g_string_new ("0x");
            for (i = 0; i < server->priv->authentication_data_length; i++)
                g_string_append_printf (cookie, "%02X", server->priv->authentication_data[i]);
            g_string_append_printf (command, " -cookie %s", cookie->str);
            g_string_free (cookie, TRUE);
        }
    }
    else
        g_string_append (command, " -nolisten tcp");

    if (server->priv->vt >= 0)
        g_string_append_printf (command, " vt%d", server->priv->vt);

    if (server->priv->no_root)
        g_string_append (command, " -nr");

    g_debug ("Launching X Server");

    result = child_process_start (CHILD_PROCESS (server),
                                  NULL, /* Username (run as current user) */
                                  NULL, /* Environment (inherit parent) */
                                  command->str,
                                  FALSE,
                                  &error);
    g_string_free (command, TRUE);
    if (!result)
        g_warning ("Unable to create display: %s", error->message);
    else
        g_debug ("Waiting for ready signal from X server :%d", server->priv->display_number);
    g_clear_error (&error);

    return result;
}

void
xserver_disconnect_clients (XServer *server)
{
    server->priv->ready = FALSE;
    child_process_signal (CHILD_PROCESS (server), SIGHUP);
}

static void
xserver_init (XServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_TYPE, XServerPrivate);
    server->priv->authentication_name = g_strdup ("");
    server->priv->vt = -1;
}

static void
xserver_finalize (GObject *object)
{
    XServer *self;
  
    self = XSERVER (object);

    if (self->priv->connection)
        xcb_disconnect (self->priv->connection);

    g_free (self->priv->command);
    g_free (self->priv->config_file);
    g_free (self->priv->layout);
    g_free (self->priv->hostname);
    g_free (self->priv->authentication_name);
    g_free (self->priv->authentication_data);
    g_free (self->priv->address);
    if (self->priv->authorization)
        g_object_unref (self->priv->authorization);
    g_free (self->priv->authorization_path);
    if (self->priv->authorization_file)
    {
        g_file_delete (self->priv->authorization_file, NULL, NULL);
        g_object_unref (self->priv->authorization_file);
    }

    G_OBJECT_CLASS (xserver_parent_class)->finalize (object);
}

static void
xserver_got_signal (ChildProcess *process, int signum)
{
    XServer *server = XSERVER (process);

    if (signum == SIGUSR1 && !server->priv->ready)
    {
        server->priv->ready = TRUE;
        g_debug ("Got signal from X server :%d", server->priv->display_number);

        xserver_connect (server);

        g_signal_emit (server, signals[READY], 0);
    }
}

static void
xserver_class_init (XServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    ChildProcessClass *parent_class = CHILD_PROCESS_CLASS (klass);

    object_class->finalize = xserver_finalize;
    parent_class->got_signal = xserver_got_signal;

    g_type_class_add_private (klass, sizeof (XServerPrivate));

    signals[READY] =
        g_signal_new ("ready",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XServerClass, ready),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    servers = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, NULL);
}
