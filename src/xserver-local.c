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
#include <errno.h>
#include <glib/gstdio.h>

#include "xserver-local.h"
#include "configuration.h"
#include "process.h"
#include "vt.h"
#include "plymouth.h"

struct XServerLocalPrivate
{
    /* X server process */
    Process *xserver_process;
  
    /* File to log to */
    gchar *log_file;    

    /* Command to run the X server */
    gchar *command;

    /* Config file to use */
    gchar *config_file;

    /* Server layout to use */
    gchar *layout;

    /* TRUE if TCP/IP connections are allowed */
    gboolean allow_tcp;

    /* Authority file */
    GFile *authority_file;

    /* XDMCP server to connect to */
    gchar *xdmcp_server;

    /* XDMCP port to connect to */
    guint xdmcp_port;

    /* XDMCP key to use */
    gchar *xdmcp_key;

    /* ID to report to Mir */
    gint mir_id;

    /* TRUE when received ready signal */
    gboolean got_signal;

    /* VT to run on */
    gint vt;
  
    /* TRUE if holding a reference to the VT */
    gboolean have_vt_ref;
  
    /* TRUE if replacing Plymouth */
    gboolean replacing_plymouth;
};

G_DEFINE_TYPE (XServerLocal, xserver_local, XSERVER_TYPE);

static GList *display_numbers = NULL;

static gboolean
display_number_in_use (guint display_number)
{
    GList *link;
    gchar *path;
    gboolean result;

    for (link = display_numbers; link; link = link->next)
    {
        guint number = GPOINTER_TO_UINT (link->data);
        if (number == display_number)
            return TRUE;
    }

    path = g_strdup_printf ("/tmp/.X%d-lock", display_number);
    result = g_file_test (path, G_FILE_TEST_EXISTS);
    g_free (path);

    return result;
}

guint
xserver_local_get_unused_display_number (void)
{
    guint number;

    number = config_get_integer (config_get_instance (), "LightDM", "minimum-display-number");
    while (display_number_in_use (number))
        number++;

    display_numbers = g_list_append (display_numbers, GUINT_TO_POINTER (number));

    return number;
}

void
xserver_local_release_display_number (guint display_number)
{
    GList *link;
    for (link = display_numbers; link; link = link->next)
    {
        guint number = GPOINTER_TO_UINT (link->data);
        if (number == display_number)
        {
            display_numbers = g_list_remove_link (display_numbers, link);
            return;
        }
    }
}

XServerLocal *
xserver_local_new (void)
{
    XServerLocal *self = g_object_new (XSERVER_LOCAL_TYPE, NULL);
    gchar *name;

    xserver_set_display_number (XSERVER (self), xserver_local_get_unused_display_number ());

    name = g_strdup_printf ("x-%d", xserver_get_display_number (XSERVER (self)));
    display_server_set_name (DISPLAY_SERVER (self), name);
    g_free (name);

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
    if (self->priv->vt >= 0)
    {
        vt_ref (self->priv->vt);
        self->priv->have_vt_ref = TRUE;
    }

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
xserver_local_set_allow_tcp (XServerLocal *server, gboolean allow_tcp)
{
    g_return_if_fail (server != NULL);
    server->priv->allow_tcp = allow_tcp;
}

void
xserver_local_set_xdmcp_server (XServerLocal *server, const gchar *hostname)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->xdmcp_server);
    server->priv->xdmcp_server = g_strdup (hostname);
    display_server_set_start_local_sessions (DISPLAY_SERVER (server), hostname == NULL);
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

void
xserver_local_set_mir_id (XServerLocal *server, gint id)
{
    g_return_if_fail (server != NULL);
    server->priv->mir_id = id;

    if (server->priv->have_vt_ref)
    {
        vt_unref (server->priv->vt);
        server->priv->have_vt_ref = FALSE;
    }
    server->priv->vt = -1;
}

gint
xserver_local_get_vt (XServerLocal *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->vt;
}

gchar *
xserver_local_get_authority_file_path (XServerLocal *server)
{
    g_return_val_if_fail (server != NULL, 0);
    if (server->priv->authority_file)
        return g_file_get_path (server->priv->authority_file);
    return NULL;
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
    g_free (absolute_binary);

    g_strfreev (tokens);

    return absolute_command;
}

static void
run_cb (Process *process, XServerLocal *server)
{
    int fd;

    /* Make input non-blocking */
    fd = open ("/dev/null", O_RDONLY);
    dup2 (fd, STDIN_FILENO);
    close (fd);

    /* Redirect output to logfile */
    if (server->priv->log_file)
    {
         int fd;

         fd = g_open (server->priv->log_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
         if (fd < 0)
             g_warning ("Failed to open log file %s: %s", server->priv->log_file, g_strerror (errno));
         else
         {
             dup2 (fd, STDOUT_FILENO);
             dup2 (fd, STDERR_FILENO);
             close (fd);
         }
    }

    /* Set SIGUSR1 to ignore so the X server can indicate it when it is ready */
    signal (SIGUSR1, SIG_IGN);
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

        // FIXME: Check return value
        DISPLAY_SERVER_CLASS (xserver_local_parent_class)->start (DISPLAY_SERVER (server));
    }
}

static void
stopped_cb (Process *process, XServerLocal *server)
{
    g_debug ("X server stopped");

    xserver_local_release_display_number (xserver_get_display_number (XSERVER (server)));
  
    if (xserver_get_authority (XSERVER (server)) && server->priv->authority_file)
    {
        GError *error = NULL;
        gchar *path;

        path = g_file_get_path (server->priv->authority_file);      
        g_debug ("Removing X server authority %s", path);
        g_free (path);

        g_file_delete (server->priv->authority_file, NULL, &error);
        if (error)
            g_debug ("Error removing authority: %s", error->message);
        g_clear_error (&error);

        g_object_unref (server->priv->authority_file);
        server->priv->authority_file = NULL;
    }

    if (server->priv->have_vt_ref)
    {
        vt_unref (server->priv->vt);
        server->priv->have_vt_ref = FALSE;
    }  

    if (server->priv->replacing_plymouth && plymouth_get_is_running ())
    {
        g_debug ("Stopping Plymouth, X server failed to start");
        server->priv->replacing_plymouth = FALSE;
        plymouth_quit (FALSE);
    }

    DISPLAY_SERVER_CLASS (xserver_local_parent_class)->stop (DISPLAY_SERVER (server));
}

static void
write_authority_file (XServerLocal *server)
{
    XAuthority *authority;
    gchar *path;
    GError *error = NULL;

    authority = xserver_get_authority (XSERVER (server));
    if (!authority)
        return;

    /* Get file to write to if have authority */
    if (!server->priv->authority_file)
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

    path = g_file_get_path (server->priv->authority_file);
    g_debug ("Writing X server authority to %s", path);
    g_free (path);

    xauth_write (authority, XAUTH_WRITE_MODE_REPLACE, server->priv->authority_file, &error);
    if (error)
        g_warning ("Failed to write authority: %s", error->message);
    g_clear_error (&error);
}

static gboolean
xserver_local_start (DisplayServer *display_server)
{
    XServerLocal *server = XSERVER_LOCAL (display_server);
    gboolean result;
    gchar *filename, *dir, *absolute_command;
    gchar hostname[1024], *number;
    GString *command;

    g_return_val_if_fail (server->priv->xserver_process == NULL, FALSE);

    server->priv->got_signal = FALSE;

    g_return_val_if_fail (server->priv->command != NULL, FALSE);

    server->priv->xserver_process = process_new ();
    process_set_clear_environment (server->priv->xserver_process, TRUE);
    g_signal_connect (server->priv->xserver_process, "run", G_CALLBACK (run_cb), server);  
    g_signal_connect (server->priv->xserver_process, "got-signal", G_CALLBACK (got_signal_cb), server);
    g_signal_connect (server->priv->xserver_process, "stopped", G_CALLBACK (stopped_cb), server);

    /* Setup logging */
    filename = g_strdup_printf ("%s.log", display_server_get_name (display_server));
    dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    server->priv->log_file = g_build_filename (dir, filename, NULL);
    g_debug ("Logging to %s", server->priv->log_file);
    g_free (filename);
    g_free (dir);

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
        xserver_set_authority (XSERVER (server), xauth_new_cookie (XAUTH_FAMILY_LOCAL, (guint8*) hostname, strlen (hostname), number));
    g_free (number);
    write_authority_file (server);
    if (server->priv->authority_file)
    {
        gchar *path = g_file_get_path (server->priv->authority_file);
        g_string_append_printf (command, " -auth %s", path);
        g_free (path);
    }

    /* Setup for running inside Mir */
    if (server->priv->mir_id >= 0)
        g_string_append_printf (command, " -mir %d", server->priv->mir_id);

    /* Connect to a remote server using XDMCP */
    if (server->priv->xdmcp_server != NULL)
    {
        if (server->priv->xdmcp_port != 0)
            g_string_append_printf (command, " -port %d", server->priv->xdmcp_port);
        g_string_append_printf (command, " -query %s", server->priv->xdmcp_server);
        if (server->priv->xdmcp_key)
            g_string_append_printf (command, " -cookie %s", server->priv->xdmcp_key);
    }
    else if (!server->priv->allow_tcp)
        g_string_append (command, " -nolisten tcp");

    if (server->priv->vt >= 0)
        g_string_append_printf (command, " vt%d -novtswitch", server->priv->vt);

    if (server->priv->replacing_plymouth)
        g_string_append (command, " -background none");
    process_set_command (server->priv->xserver_process, command->str);
    g_string_free (command, TRUE);

    g_debug ("Launching X Server");

    /* If running inside another display then pass through those variables */
    if (g_getenv ("DISPLAY"))
    {
        process_set_env (server->priv->xserver_process, "DISPLAY", g_getenv ("DISPLAY"));
        if (g_getenv ("XAUTHORITY"))
            process_set_env (server->priv->xserver_process, "XAUTHORITY", g_getenv ("XAUTHORITY"));
        else
        {
            gchar *path;
            path = g_build_filename (g_get_home_dir (), ".Xauthority", NULL);
            process_set_env (server->priv->xserver_process, "XAUTHORITY", path);
        }
    }

    /* Variable required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
    {
        process_set_env (server->priv->xserver_process, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));
        process_set_env (server->priv->xserver_process, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
        process_set_env (server->priv->xserver_process, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    }

    result = process_start (server->priv->xserver_process);

    if (result)
        g_debug ("Waiting for ready signal from X server :%d", xserver_get_display_number (XSERVER (server)));

    if (!result)
        stopped_cb (server->priv->xserver_process, XSERVER_LOCAL (server));

    return result;
}
 
static void
xserver_local_stop (DisplayServer *server)
{
    process_stop (XSERVER_LOCAL (server)->priv->xserver_process);
}

static gboolean
xserver_local_get_is_stopped (DisplayServer *server)
{
    return process_get_pid (XSERVER_LOCAL (server)->priv->xserver_process) == 0;
}

static void
xserver_local_init (XServerLocal *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_LOCAL_TYPE, XServerLocalPrivate);
    server->priv->vt = -1;
    server->priv->command = g_strdup ("X");
    server->priv->mir_id = -1;
}

static void
xserver_local_finalize (GObject *object)
{
    XServerLocal *self;

    self = XSERVER_LOCAL (object);  

    if (self->priv->xserver_process)
        g_object_unref (self->priv->xserver_process);
    g_free (self->priv->log_file);
    g_free (self->priv->command);
    g_free (self->priv->config_file);
    g_free (self->priv->layout);
    g_free (self->priv->xdmcp_server);
    g_free (self->priv->xdmcp_key);
    if (self->priv->authority_file)
        g_object_unref (self->priv->authority_file);
    if (self->priv->have_vt_ref)
        vt_unref (self->priv->vt);

    G_OBJECT_CLASS (xserver_local_parent_class)->finalize (object);
}

static void
xserver_local_class_init (XServerLocalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->start = xserver_local_start;
    display_server_class->stop = xserver_local_stop;
    display_server_class->get_is_stopped = xserver_local_get_is_stopped;
    object_class->finalize = xserver_local_finalize;

    g_type_class_add_private (klass, sizeof (XServerLocalPrivate));
}
