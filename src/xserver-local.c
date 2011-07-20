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
#include <fcntl.h>
#include <sys/stat.h>

#include "xserver-local.h"
#include "configuration.h"
#include "process.h"
#include "vt.h"
#include "plymouth.h"

struct XServerLocalPrivate
{
    /* X server process */
    Process *xserver_process;

    /* Path of file to log to */
    gchar *log_file;
  
    /* Command to run the X server */
    gchar *command;

    /* Config file to use */
    gchar *config_file;

    /* Server layout to use */
    gchar *layout;
  
    /* Authority file */
    GFile *authority_file;

    /* XDMCP server to connect to */
    gchar *xdmcp_server;

    /* XDMCP port to connect to */
    guint xdmcp_port;

    /* XDMCP key to use */
    gchar *xdmcp_key;

    /* TRUE when received ready signal */
    gboolean got_signal;

    /* VT to run on */
    gint vt;
  
    /* TRUE if replacing Plymouth */
    gboolean replacing_plymouth;
};

G_DEFINE_TYPE (XServerLocal, xserver_local, XSERVER_TYPE);

static GList *display_numbers = NULL;

static guint
get_free_display_number (void)
{
    guint number;

    number = config_get_integer (config_get_instance (), "LightDM", "minimum-display-number");
    while (g_list_find (display_numbers, GINT_TO_POINTER (number)))
        number++;

    display_numbers = g_list_append (display_numbers, GINT_TO_POINTER (number));

    return number;
}

static void
release_display_number (guint number)
{
    display_numbers = g_list_remove (display_numbers, GINT_TO_POINTER (number));
}

XServerLocal *
xserver_local_new (void)
{
    XServerLocal *self = g_object_new (XSERVER_LOCAL_TYPE, NULL);

    xserver_set_display_number (XSERVER (self), get_free_display_number ());
  
    /* Replace Plymouth if it is running */
    if (plymouth_get_is_active () && plymouth_has_active_vt ())
    {
        gint active_vt = vt_get_active ();
        if (active_vt >= vt_get_min ())
        {
            g_debug ("X server %s will replace Plymouth", xserver_get_address (XSERVER (self)));
            self->priv->replacing_plymouth = TRUE;
            self->priv->vt = active_vt;
            plymouth_deactivate ();
        }
        else
            g_debug ("Plymouth is running on VT %d, but this is less than the configured minimum of %d so not replacing it", active_vt, vt_get_min ());
    }
    if (self->priv->vt < 0)
        self->priv->vt = vt_get_unused ();

    return self;
}

void
xserver_local_set_command (XServerLocal *server, const gchar *command)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->command);
    server->priv->command = g_strdup (command);
}

void
xserver_local_set_config (XServerLocal *server, const gchar *path)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->config_file);
    server->priv->config_file = g_strdup (path);
}

void
xserver_local_set_layout (XServerLocal *server, const gchar *layout)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->layout);
    server->priv->layout = g_strdup (layout);
}

void
xserver_local_set_xdmcp_server (XServerLocal *server, const gchar *hostname)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->xdmcp_server);
    server->priv->xdmcp_server = g_strdup (hostname);
}

const gchar *
xserver_local_get_xdmcp_server (XServerLocal *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->xdmcp_server;
}

void
xserver_local_set_xdmcp_port (XServerLocal *server, guint port)
{
    g_return_if_fail (server != NULL);
    server->priv->xdmcp_port = port;
}

guint
xserver_local_get_xdmcp_port (XServerLocal *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->xdmcp_port;
}

void
xserver_local_set_xdmcp_key (XServerLocal *server, const gchar *key)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->xdmcp_key);
    server->priv->xdmcp_key = g_strdup (key);
}

gint
xserver_local_get_vt (XServerLocal *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->vt;
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

static void
got_signal_cb (Process *process, int signum, XServerLocal *server)
{
    if (signum == SIGUSR1 && !server->priv->got_signal)
    {
        server->priv->got_signal = TRUE;
        g_debug ("Got signal from X server :%d", xserver_get_display_number (XSERVER (server)));

        if (server->priv->replacing_plymouth)
        {
            g_debug ("Stopping Plymouth, X server is ready");
            server->priv->replacing_plymouth = FALSE;
            plymouth_quit (TRUE);
        }

        display_server_set_ready (DISPLAY_SERVER (server));
    }
}

static void
exit_cb (Process *process, int status, XServerLocal *server)
{
    if (status != 0)
        g_debug ("X server exited with value %d", status);
}

static void
terminated_cb (Process *process, int signum, XServerLocal *server)
{
    g_debug ("X server terminated with signal %d", signum);
}

static void
stopped_cb (Process *process, XServerLocal *server)
{
    g_debug ("X server stopped");

    g_object_unref (server->priv->xserver_process);
    server->priv->xserver_process = NULL;

    if (server->priv->replacing_plymouth && plymouth_get_is_running ())
    {
        g_debug ("Stopping Plymouth, X server failed to start");
        server->priv->replacing_plymouth = FALSE;
        plymouth_quit (FALSE);
    }

    display_server_set_stopped (DISPLAY_SERVER (server));
}

static void
write_authority_file (XServerLocal *server)
{
    XAuthority *authority;
    gchar *path;
    GError *error = NULL;

    authority = xserver_get_authority (XSERVER (server));

    /* Get file to write to if have authority */
    if (authority && !server->priv->authority_file)
    {
        gchar *run_dir, *dir;
      
        run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
        dir = g_build_filename (run_dir, "root", NULL);
        g_free (run_dir);
        g_mkdir_with_parents (dir, S_IRWXU);

        path = g_build_filename (dir, xserver_get_address (XSERVER (server)), NULL);
        g_free (dir);
        server->priv->authority_file = g_file_new_for_path (path);
        g_free (path);
    }

    /* Delete existing file if no authority */
    if (!authority)
    {
        if (server->priv->authority_file)
        {
            path = g_file_get_path (server->priv->authority_file);
            g_debug ("Deleting X server authority %s", path);
            g_free (path);

            g_file_delete (server->priv->authority_file, NULL, NULL);
            g_object_unref (server->priv->authority_file);
            server->priv->authority_file = NULL;
        }
        return;
    }

    path = g_file_get_path (server->priv->authority_file);
    g_debug ("Writing X server authority to %s", path);
    g_free (path);

    if (!xauth_write (authority, XAUTH_WRITE_MODE_SET, NULL, server->priv->authority_file, &error))
        g_warning ("Failed to write authority: %s", error->message);
    g_clear_error (&error);
}

static gboolean
xserver_local_start (DisplayServer *display_server)
{
    XServerLocal *server = XSERVER_LOCAL (display_server);
    gboolean result;
    gchar *filename, *dir, *path, *absolute_command;
    gchar hostname[1024], *number;
    GString *command;
    GError *error = NULL;

    g_return_val_if_fail (server->priv->xserver_process == NULL, FALSE);

    server->priv->got_signal = FALSE;

    g_return_val_if_fail (server->priv->command != NULL, FALSE);

    server->priv->xserver_process = process_new ();
    g_signal_connect (server->priv->xserver_process, "got-signal", G_CALLBACK (got_signal_cb), server);
    g_signal_connect (server->priv->xserver_process, "exited", G_CALLBACK (exit_cb), server);
    g_signal_connect (server->priv->xserver_process, "terminated", G_CALLBACK (terminated_cb), server);
    g_signal_connect (server->priv->xserver_process, "stopped", G_CALLBACK (stopped_cb), server);

    /* Setup logging */
    filename = g_strdup_printf ("%s.log", xserver_get_address (XSERVER (server)));
    dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    path = g_build_filename (dir, filename, NULL);
    g_debug ("Logging to %s", path);
    process_set_log_file (server->priv->xserver_process, path);
    g_free (filename);
    g_free (dir);
    g_free (path);

    absolute_command = get_absolute_command (server->priv->command);
    if (!absolute_command)
    {
        g_debug ("Can't launch X server %s, not found in path", server->priv->command);
        stopped_cb (server->priv->xserver_process, XSERVER_LOCAL (server));
        return FALSE;
    }
    command = g_string_new (absolute_command);
    g_free (absolute_command);

    g_string_append_printf (command, " :%d", xserver_get_display_number (XSERVER (server)));

    if (server->priv->config_file)
        g_string_append_printf (command, " -config %s", server->priv->config_file);

    if (server->priv->layout)
        g_string_append_printf (command, " -layout %s", server->priv->layout);

    gethostname (hostname, 1024);
    number = g_strdup_printf ("%d", xserver_get_display_number (XSERVER (server)));
    if (!server->priv->xdmcp_key)
        xserver_set_authority (XSERVER (server), xauth_new_cookie (XAUTH_FAMILY_LOCAL, hostname, number));
    g_free (number);
    write_authority_file (server);
    if (server->priv->authority_file)
    {
        gchar *path = g_file_get_path (server->priv->authority_file);
        g_string_append_printf (command, " -auth %s", path);
        g_free (path);
    }

    /* Connect to a remote server using XDMCP */
    if (server->priv->xdmcp_server != NULL)
    {
        if (server->priv->xdmcp_port != 0)
            g_string_append_printf (command, " -port %d", server->priv->xdmcp_port);
        g_string_append_printf (command, " -query %s", server->priv->xdmcp_server);
        if (server->priv->xdmcp_key)
            g_string_append_printf (command, " -cookie %s", server->priv->xdmcp_key);
    }
    else
        g_string_append (command, " -nolisten tcp");

    if (server->priv->vt >= 0)
        g_string_append_printf (command, " vt%d", server->priv->vt);

    if (server->priv->replacing_plymouth)
        g_string_append (command, " -background none");

    g_debug ("Launching X Server");

    /* If running inside another display then pass through those variables */
    if (g_getenv ("DISPLAY"))
        process_set_env (server->priv->xserver_process, "DISPLAY", g_getenv ("DISPLAY"));
    if (g_getenv ("XAUTHORITY"))
        process_set_env (server->priv->xserver_process, "XAUTHORITY", g_getenv ("XAUTHORITY"));

    /* Variable required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"))
    {
        process_set_env (server->priv->xserver_process, "LIGHTDM_TEST_STATUS_SOCKET", g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"));
        process_set_env (server->priv->xserver_process, "LIGHTDM_TEST_CONFIG", g_getenv ("LIGHTDM_TEST_CONFIG"));
        process_set_env (server->priv->xserver_process, "LIGHTDM_TEST_HOME_DIR", g_getenv ("LIGHTDM_TEST_HOME_DIR"));
        process_set_env (server->priv->xserver_process, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    }

    result = process_start (server->priv->xserver_process,
                            user_get_current (),
                            NULL,
                            command->str,
                            &error);
    g_string_free (command, TRUE);
    if (!result)
        g_warning ("Unable to create display: %s", error->message);
    else
        g_debug ("Waiting for ready signal from X server :%d", xserver_get_display_number (XSERVER (server)));
    g_clear_error (&error);

    if (!result)
        stopped_cb (server->priv->xserver_process, XSERVER_LOCAL (server));

    return result;
}
 
static gboolean
xserver_local_restart (DisplayServer *display_server)
{
    XServerLocal *server = XSERVER_LOCAL (display_server);
    gchar hostname[1024], *number;

    /* Not running */
    if (!server->priv->xserver_process)
        return FALSE;

    /* Can only restart with not using authentication */
    if (server->priv->xdmcp_key)
        return FALSE;

    g_debug ("Generating new cookie for X server");
    gethostname (hostname, 1024);
    number = g_strdup_printf ("%d", xserver_get_display_number (XSERVER (server)));
    xserver_set_authority (XSERVER (server), xauth_new_cookie (XAUTH_FAMILY_LOCAL, hostname, number));
    g_free (number);
    write_authority_file (server);

    g_debug ("Sending signal to X server to disconnect clients");

    server->priv->got_signal = FALSE;
    process_signal (server->priv->xserver_process, SIGHUP);

    return TRUE;
}

static void
xserver_local_stop (DisplayServer *server)
{
    process_stop (XSERVER_LOCAL (server)->priv->xserver_process);
}

static void
xserver_local_init (XServerLocal *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_LOCAL_TYPE, XServerLocalPrivate);
    server->priv->vt = -1;
    server->priv->command = g_strdup ("X");
}

static void
xserver_local_finalize (GObject *object)
{
    XServerLocal *self;

    self = XSERVER_LOCAL (object);

    release_display_number (xserver_get_display_number (XSERVER (self)));

    if (self->priv->vt >= 0)
        vt_release (self->priv->vt);

    g_free (self->priv->command);
    g_free (self->priv->config_file);
    g_free (self->priv->layout);
    g_free (self->priv->xdmcp_server);
    g_free (self->priv->xdmcp_key);
    if (self->priv->authority_file)
    {
        g_file_delete (self->priv->authority_file, NULL, NULL);
        g_object_unref (self->priv->authority_file);
    }

    G_OBJECT_CLASS (xserver_local_parent_class)->finalize (object);
}

static void
xserver_local_class_init (XServerLocalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->start = xserver_local_start;
    display_server_class->restart = xserver_local_restart;
    display_server_class->stop = xserver_local_stop;
    object_class->finalize = xserver_local_finalize;

    g_type_class_add_private (klass, sizeof (XServerLocalPrivate));
}
