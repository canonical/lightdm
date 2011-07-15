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
#include "configuration.h"
#include "vt.h"

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
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->type;
}

void
xserver_set_command (XServer *server, const gchar *command)
{
    g_return_if_fail (server != NULL);

    g_free (server->priv->command);
    server->priv->command = g_strdup (command);
}

void
xserver_set_config_file (XServer *server, const gchar *config_file)
{
    g_return_if_fail (server != NULL);

    g_free (server->priv->config_file);
    server->priv->config_file = g_strdup (config_file);
}

const gchar *
xserver_get_config_file (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->config_file;
}

void
xserver_set_layout (XServer *server, const gchar *layout)
{
    g_return_if_fail (server != NULL);

    g_free (server->priv->layout);
    server->priv->layout = g_strdup (layout);
}

const gchar *
xserver_get_layout (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->layout;
}

const gchar *
xserver_get_command (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->command;
}

void
xserver_set_log_file (XServer *server, const gchar *log_file)
{
    g_return_if_fail (server != NULL);

    g_free (server->priv->log_file);
    server->priv->log_file = g_strdup (log_file);
}

const gchar *
xserver_get_log_file (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->log_file;
}
  
void
xserver_set_port (XServer *server, guint port)
{
    g_return_if_fail (server != NULL);
    server->priv->port = port;
}

guint
xserver_get_port (XServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->port;
}

const gchar *
xserver_get_hostname (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->hostname;
}

gint
xserver_get_display_number (XServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->display_number;
}

const gchar *
xserver_get_address (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);

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
    g_return_if_fail (server != NULL);

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
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->authentication_name;
}

const guchar *
xserver_get_authentication_data (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->authentication_data;
}

gsize
xserver_get_authentication_data_length (XServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->authentication_data_length;
}

static void
write_authorization_file (XServer *server)
{
    gchar *run_dir, *dir, *path;
    GError *error = NULL;

    /* Stop if not using authorization or already written */
    if (!server->priv->authorization || server->priv->authorization_file)
        return;

    run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
    dir = g_build_filename (run_dir, "root", NULL);
    g_free (run_dir);
    g_mkdir_with_parents (dir, S_IRWXU);

    path = g_build_filename (dir, xserver_get_address (server), NULL);
    g_free (dir);
    server->priv->authorization_file = g_file_new_for_path (path);

    g_debug ("Writing X server authorization to %s", path);
    g_free (path);

    if (!xauth_write (server->priv->authorization, XAUTH_WRITE_MODE_SET, NULL, server->priv->authorization_file, &error))
        g_warning ("Failed to write authorization: %s", error->message);
    g_clear_error (&error);
}

void
xserver_set_authorization (XServer *server, XAuthorization *authorization)
{
    gboolean rewrite = FALSE;

    g_return_if_fail (server != NULL);

    /* Delete existing authorization */
    if (server->priv->authorization_file)
    {
        g_file_delete (server->priv->authorization_file, NULL, NULL);
        g_object_unref (server->priv->authorization_file);
        server->priv->authorization_file = NULL;
        rewrite = TRUE;
    }

    if (server->priv->authorization)
        g_object_unref (server->priv->authorization);
    server->priv->authorization = NULL;
    if (authorization)
        server->priv->authorization = g_object_ref (authorization);

    /* If already running then change authorization immediately */
    if (rewrite)
        write_authorization_file (server);
}

XAuthorization *
xserver_get_authorization (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->authorization;
}

void
xserver_set_vt (XServer *server, gint vt)
{
    g_return_if_fail (server != NULL);
    server->priv->vt = vt;
}

gint
xserver_get_vt (XServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->vt;
}

void
xserver_set_no_root (XServer *server, gboolean no_root)
{
    g_return_if_fail (server != NULL);
    server->priv->no_root = no_root;
}

static gboolean
xserver_connect (XServer *server)
{
    gchar *xauthority = NULL;

    g_return_val_if_fail (server != NULL, FALSE);

    /* Write the authorization file */
    write_authorization_file (server);

    /* NOTE: We have to do this hack as xcb_connect_to_display_with_auth_info can't be used
     * for XDM-AUTHORIZATION-1 and the authorization data requires to know the source port */
    if (server->priv->authorization_file)
    {
        gchar *path = g_file_get_path (server->priv->authorization_file);
        xauthority = g_strdup (getenv ("XAUTHORITY"));
        setenv ("XAUTHORITY", path, TRUE);
        g_free (path);
    }

    g_debug ("Connecting to XServer %s", xserver_get_address (server));
    server->priv->connection = xcb_connect (xserver_get_address (server), NULL);
    if (xcb_connection_has_error (server->priv->connection))
        g_debug ("Error connecting to XServer %s", xserver_get_address (server));

    if (server->priv->authorization_file)
    {
        if (xauthority)
            setenv ("XAUTHORITY", xauthority, TRUE);
        else
            unsetenv ("XAUTHORITY");
        g_free (xauthority);
    }

    return xcb_connection_has_error (server->priv->connection) == 0;
}

static gchar *
get_absolute_command (const gchar *command)
{
    gchar **tokens;
    gchar *absolute_binary, *absolute_command = NULL;

    tokens = g_strsplit (command, " ", 2);

    absolute_binary = g_find_program_in_path (tokens[0]);
    if (absolute_binary)
    {
        if (tokens[1])
            absolute_command = g_strjoin (" ", absolute_binary, tokens[1], NULL);
        else
            absolute_command = g_strdup (absolute_binary);
    }

    g_strfreev (tokens);

    return absolute_command;
}

gboolean
xserver_start (XServer *server)
{
    GError *error = NULL;
    gboolean result;
    gchar *absolute_command;
    GString *command;

    g_return_val_if_fail (server != NULL, FALSE);
    //g_return_val_if_fail (server->priv->pid == 0, FALSE);
  
    server->priv->ready = FALSE;
 
    /* Check if we can connect to the remote server */
    if (server->priv->type == XSERVER_TYPE_REMOTE)
    {
        if (!xserver_connect (server))
            return FALSE;

        server->priv->ready = TRUE;
        g_signal_emit (server, signals[READY], 0);
        return TRUE;
    }

    g_return_val_if_fail (server->priv->command != NULL, FALSE);

    absolute_command = get_absolute_command (server->priv->command);
    if (!absolute_command)
    {
        g_debug ("Can't launch X server %s, not found in path", server->priv->command);
        return FALSE;
    }
    command = g_string_new (absolute_command);
    g_free (absolute_command);

    /* Write the authorization file */
    write_authorization_file (server);

    g_string_append_printf (command, " :%d", server->priv->display_number);

    if (server->priv->config_file)
        g_string_append_printf (command, " -config %s", server->priv->config_file);

    if (server->priv->layout)
        g_string_append_printf (command, " -layout %s", server->priv->layout);

    if (server->priv->authorization)
    {
        gchar *path = g_file_get_path (server->priv->authorization_file);
        g_string_append_printf (command, " -auth %s", path);
        g_free (path);
    }
  
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
        g_string_append (command, " -background none");

    g_debug ("Launching X Server");

    /* If running inside another display then pass through those variables */
    if (getenv ("DISPLAY"))
        child_process_set_env (CHILD_PROCESS (server), "DISPLAY", getenv ("DISPLAY"));
    if (getenv ("XAUTHORITY"))
        child_process_set_env (CHILD_PROCESS (server), "XAUTHORITY", getenv ("XAUTHORITY"));

    /* Variable required for regression tests */
    if (getenv ("LIGHTDM_TEST_STATUS_SOCKET"))
    {
        child_process_set_env (CHILD_PROCESS (server), "LIGHTDM_TEST_STATUS_SOCKET", getenv ("LIGHTDM_TEST_STATUS_SOCKET"));
        child_process_set_env (CHILD_PROCESS (server), "LIGHTDM_TEST_CONFIG", getenv ("LIGHTDM_TEST_CONFIG"));
        child_process_set_env (CHILD_PROCESS (server), "LIGHTDM_TEST_HOME_DIR", getenv ("LIGHTDM_TEST_HOME_DIR"));
        child_process_set_env (CHILD_PROCESS (server), "LD_LIBRARY_PATH", getenv ("LD_LIBRARY_PATH"));
    }

    result = child_process_start (CHILD_PROCESS (server),
                                  user_get_current (),
                                  NULL,
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

gboolean
xserver_get_is_running (XServer *server)
{
    g_return_val_if_fail (server != NULL, FALSE);

    return child_process_get_is_running (CHILD_PROCESS (server));
}
  
void
xserver_disconnect_clients (XServer *server)
{
    g_return_if_fail (server != NULL);
  
    g_debug ("Sending signal to X server to disconnect clients");

    server->priv->ready = FALSE;
    child_process_signal (CHILD_PROCESS (server), SIGHUP);
}

void
xserver_stop (XServer *server)
{
    g_return_if_fail (server != NULL);

    child_process_stop (CHILD_PROCESS (server));
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
  
    if (self->priv->vt >= 0)
        vt_release (self->priv->vt);

    g_free (self->priv->command);
    g_free (self->priv->config_file);
    g_free (self->priv->layout);
    g_free (self->priv->hostname);
    g_free (self->priv->authentication_name);
    g_free (self->priv->authentication_data);
    g_free (self->priv->address);
    if (self->priv->authorization)
        g_object_unref (self->priv->authorization);
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
