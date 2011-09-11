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

#include "xserver-xvnc.h"
#include "configuration.h"
#include "process.h"

struct XServerXVNCPrivate
{
    /* X server process */
    Process *xserver_process;

    /* Authority file */
    GFile *authority_file;

    /* File descriptor to use for standard input */
    gint stdin_fd;

    /* TRUE when received ready signal */
    gboolean got_signal;
};

G_DEFINE_TYPE (XServerXVNC, xserver_xvnc, XSERVER_TYPE);

static guint
get_free_display_number (void)
{
    guint number;

    number = config_get_integer (config_get_instance (), "LightDM", "minimum-display-number");
    while (TRUE)
    {
        gchar *path;
        gboolean result;
  
        path = g_strdup_printf ("/tmp/.X%d-lock", number);
        result = g_file_test (path, G_FILE_TEST_EXISTS);
        g_free (path);

        if (!result)
            break;

        number++;
    }
  
    return number;
}

XServerXVNC *
xserver_xvnc_new (void)
{
    XServerXVNC *self = g_object_new (XSERVER_XVNC_TYPE, NULL);

    xserver_set_display_number (XSERVER (self), get_free_display_number ());
  
    return self;
}

void
xserver_xvnc_set_stdin (XServerXVNC *server, int fd)
{
    g_return_if_fail (server != NULL);
    server->priv->stdin_fd = fd;
}

int
xserver_xvnc_get_stdin (XServerXVNC *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->stdin_fd;
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
run_cb (Process *process, XServerXVNC *server)
{
    /* Connect input */
    dup2 (server->priv->stdin_fd, STDIN_FILENO);

    /* Set SIGUSR1 to ignore so the X server can indicate it when it is ready */
    signal (SIGUSR1, SIG_IGN);
}

static void
got_signal_cb (Process *process, int signum, XServerXVNC *server)
{
    if (signum == SIGUSR1 && !server->priv->got_signal)
    {
        server->priv->got_signal = TRUE;
        g_debug ("Got signal from Xvnc server :%d", xserver_get_display_number (XSERVER (server)));

        // FIXME: Check return value
        DISPLAY_SERVER_CLASS (xserver_xvnc_parent_class)->start (DISPLAY_SERVER (server));
    }
}

static void
stopped_cb (Process *process, XServerXVNC *server)
{
    GError *error = NULL;
    gchar *path;
  
    g_debug ("Xvnc server stopped");

    g_object_unref (server->priv->xserver_process);
    server->priv->xserver_process = NULL;

    path = g_file_get_path (server->priv->authority_file);
    g_debug ("Removing X server authority %s", path);
    g_free (path);

    g_file_delete (server->priv->authority_file, NULL, &error);
    if (error)
        g_debug ("Error removing authority: %s", error->message);
    g_clear_error (&error);
    g_object_unref (server->priv->authority_file);
    server->priv->authority_file = NULL;

    DISPLAY_SERVER_CLASS (xserver_xvnc_parent_class)->stop (DISPLAY_SERVER (server));
}

static gboolean
xserver_xvnc_start (DisplayServer *display_server)
{
    XServerXVNC *server = XSERVER_XVNC (display_server);
    XAuthority *authority;
    gboolean result;
    gchar *filename, *run_dir, *dir, *path, *absolute_command, *command;
    gchar hostname[1024], *number;
    GError *error = NULL;

    g_return_val_if_fail (server->priv->xserver_process == NULL, FALSE);

    server->priv->got_signal = FALSE;

    server->priv->xserver_process = process_new ();
    g_signal_connect (server->priv->xserver_process, "run", G_CALLBACK (run_cb), server);  
    g_signal_connect (server->priv->xserver_process, "got-signal", G_CALLBACK (got_signal_cb), server);
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

    absolute_command = get_absolute_command ("Xvnc");
    if (!absolute_command)
    {
        g_debug ("Can't launch Xvnc, not found in path");
        stopped_cb (server->priv->xserver_process, XSERVER_XVNC (server));
        return FALSE;
    }

    gethostname (hostname, 1024);
    number = g_strdup_printf ("%d", xserver_get_display_number (XSERVER (server)));
    authority = xauth_new_cookie (XAUTH_FAMILY_LOCAL, (guint8*) hostname, strlen (hostname), number);

    xserver_set_authority (XSERVER (server), authority);

    run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
    dir = g_build_filename (run_dir, "root", NULL);
    g_free (run_dir);
    g_mkdir_with_parents (dir, S_IRWXU);

    path = g_build_filename (dir, xserver_get_address (XSERVER (server)), NULL);
    g_free (dir);
    server->priv->authority_file = g_file_new_for_path (path);
    g_free (path);

    path = g_file_get_path (server->priv->authority_file);
    g_debug ("Writing X server authority to %s", path);

    xauth_write (authority, XAUTH_WRITE_MODE_REPLACE, NULL, server->priv->authority_file, &error);
    if (error)
        g_warning ("Failed to write authority: %s", error->message);
    g_clear_error (&error);

    command = g_strdup_printf ("%s :%d -auth %s -inetd -nolisten tcp -nolisten unix",
                               absolute_command,
                               xserver_get_display_number (XSERVER (server)),
                               path);
    g_free (absolute_command);
    g_free (path);

    process_set_command (server->priv->xserver_process, command);
    g_free (command);

    g_debug ("Launching Xvnc server");

    process_set_user (server->priv->xserver_process, user_get_current ());
    result = process_start (server->priv->xserver_process);

    if (result)
        g_debug ("Waiting for ready signal from Xvnc server :%d", xserver_get_display_number (XSERVER (server)));

    if (!result)
        stopped_cb (server->priv->xserver_process, XSERVER_XVNC (server));

    return result;
}
 
static void
xserver_xvnc_stop (DisplayServer *server)
{
    process_stop (XSERVER_XVNC (server)->priv->xserver_process);
}

static void
xserver_xvnc_init (XServerXVNC *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_XVNC_TYPE, XServerXVNCPrivate);
}

static void
xserver_xvnc_finalize (GObject *object)
{
    XServerXVNC *self;

    self = XSERVER_XVNC (object);

    if (self->priv->xserver_process)
        g_object_unref (self->priv->xserver_process);
    if (self->priv->authority_file)
        g_object_unref (self->priv->authority_file);

    G_OBJECT_CLASS (xserver_xvnc_parent_class)->finalize (object);
}

static void
xserver_xvnc_class_init (XServerXVNCClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->start = xserver_xvnc_start;
    display_server_class->stop = xserver_xvnc_stop;
    object_class->finalize = xserver_xvnc_finalize;

    g_type_class_add_private (klass, sizeof (XServerXVNCPrivate));
}
