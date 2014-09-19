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
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <glib/gstdio.h>

#include "x-server-xvnc.h"
#include "configuration.h"
#include "x-server-local.h"
#include "process.h"

struct XServerXVNCPrivate
{
    /* X server process */
    Process *x_server_process;

    /* Command to run the X server */
    gchar *command;

    /* Authority file */
    gchar *authority_file;

    /* File descriptor to use for standard input */
    gint socket_fd;

    /* Geometry and colour depth */
    gint width, height, depth;

    /* TRUE when received ready signal */
    gboolean got_signal;
};

G_DEFINE_TYPE (XServerXVNC, x_server_xvnc, X_SERVER_TYPE);

XServerXVNC *
x_server_xvnc_new (void)
{
    XServerXVNC *self = g_object_new (X_SERVER_XVNC_TYPE, NULL);
    gchar *name;

    x_server_set_display_number (X_SERVER (self), x_server_local_get_unused_display_number ());

    name = g_strdup_printf ("xvnc-%d", x_server_get_display_number (X_SERVER (self)));
    display_server_set_name (DISPLAY_SERVER (self), name);
    g_free (name);

    return self;
}

void
x_server_xvnc_set_command (XServerXVNC *server, const gchar *command)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->command);
    server->priv->command = g_strdup (command);
}

void
x_server_xvnc_set_socket (XServerXVNC *server, int fd)
{
    g_return_if_fail (server != NULL);
    server->priv->socket_fd = fd;
}

int
x_server_xvnc_get_socket (XServerXVNC *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->socket_fd;
}

void
x_server_xvnc_set_geometry (XServerXVNC *server, gint width, gint height)
{
    g_return_if_fail (server != NULL);
    server->priv->width = width;
    server->priv->height = height;
}

void
x_server_xvnc_set_depth (XServerXVNC *server, gint depth)
{
    g_return_if_fail (server != NULL);
    server->priv->depth = depth;
}

const gchar *
x_server_xvnc_get_authority_file_path (XServerXVNC *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->authority_file;
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
run_cb (Process *process, gpointer user_data)
{
    XServerXVNC *server = user_data;

    /* Connect input */
    dup2 (server->priv->socket_fd, STDIN_FILENO);
    dup2 (server->priv->socket_fd, STDOUT_FILENO);
    close (server->priv->socket_fd);

    /* Set SIGUSR1 to ignore so the X server can indicate it when it is ready */
    signal (SIGUSR1, SIG_IGN);
}

static void
got_signal_cb (Process *process, int signum, XServerXVNC *server)
{
    if (signum == SIGUSR1 && !server->priv->got_signal)
    {
        server->priv->got_signal = TRUE;
        l_debug (server, "Got signal from Xvnc server :%d", x_server_get_display_number (X_SERVER (server)));

        // FIXME: Check return value
        DISPLAY_SERVER_CLASS (x_server_xvnc_parent_class)->start (DISPLAY_SERVER (server));
    }
}

static void
stopped_cb (Process *process, XServerXVNC *server)
{
    l_debug (server, "Xvnc server stopped");

    g_object_unref (server->priv->x_server_process);
    server->priv->x_server_process = NULL;

    x_server_local_release_display_number (x_server_get_display_number (X_SERVER (server)));

    l_debug (server, "Removing X server authority %s", server->priv->authority_file);

    g_unlink (server->priv->authority_file);
    g_free (server->priv->authority_file);
    server->priv->authority_file = NULL;

    DISPLAY_SERVER_CLASS (x_server_xvnc_parent_class)->stop (DISPLAY_SERVER (server));
}

static gboolean
x_server_xvnc_get_can_share (DisplayServer *server)
{
    return TRUE;
}

static gboolean
x_server_xvnc_start (DisplayServer *display_server)
{
    XServerXVNC *server = X_SERVER_XVNC (display_server);
    XAuthority *authority;
    gboolean result;
    gchar *filename, *run_dir, *dir, *log_file, *absolute_command;
    GString *command;
    gchar hostname[1024], *number;
    GError *error = NULL;

    g_return_val_if_fail (server->priv->x_server_process == NULL, FALSE);

    server->priv->got_signal = FALSE;

    server->priv->x_server_process = process_new (run_cb, server);
    process_set_clear_environment (server->priv->x_server_process, TRUE);
    g_signal_connect (server->priv->x_server_process, "got-signal", G_CALLBACK (got_signal_cb), server);
    g_signal_connect (server->priv->x_server_process, "stopped", G_CALLBACK (stopped_cb), server);

    /* Setup logging */
    filename = g_strdup_printf ("%s.log", display_server_get_name (display_server));
    dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    log_file = g_build_filename (dir, filename, NULL);
    process_set_log_file (server->priv->x_server_process, log_file, FALSE);
    l_debug (display_server, "Logging to %s", log_file);
    g_free (log_file);
    g_free (filename);
    g_free (dir);

    absolute_command = get_absolute_command (server->priv->command);
    if (!absolute_command)
    {
        l_debug (display_server, "Can't launch X server %s, not found in path", server->priv->command);
        stopped_cb (server->priv->x_server_process, X_SERVER_XVNC (server));
        return FALSE;
    }

    gethostname (hostname, 1024);
    number = g_strdup_printf ("%d", x_server_get_display_number (X_SERVER (server)));
    authority = x_authority_new_cookie (XAUTH_FAMILY_LOCAL, (guint8*) hostname, strlen (hostname), number);

    x_server_set_authority (X_SERVER (server), authority);

    run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
    dir = g_build_filename (run_dir, "root", NULL);
    g_free (run_dir);
    if (g_mkdir_with_parents (dir, S_IRWXU) < 0)
        l_warning (display_server, "Failed to make authority directory %s: %s", dir, strerror (errno));

    server->priv->authority_file = g_build_filename (dir, x_server_get_address (X_SERVER (server)), NULL);
    g_free (dir);

    l_debug (display_server, "Writing X server authority to %s", server->priv->authority_file);

    x_authority_write (authority, XAUTH_WRITE_MODE_REPLACE, server->priv->authority_file, &error);
    if (error)
        l_warning (display_server, "Failed to write authority: %s", error->message);
    g_clear_error (&error);

    command = g_string_new (absolute_command);
    g_free (absolute_command);

    g_string_append_printf (command, " :%d", x_server_get_display_number (X_SERVER (server)));
    g_string_append_printf (command, " -auth %s", server->priv->authority_file);
    g_string_append (command, " -inetd -nolisten tcp");
    if (server->priv->width > 0 && server->priv->height > 0)
        g_string_append_printf (command, " -geometry %dx%d", server->priv->width, server->priv->height);
    if (server->priv->depth > 0)
        g_string_append_printf (command, " -depth %d", server->priv->depth);

    process_set_command (server->priv->x_server_process, command->str);
    g_string_free (command, TRUE);

    l_debug (display_server, "Launching Xvnc server");

    /* Variable required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
    {
        process_set_env (server->priv->x_server_process, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));
        process_set_env (server->priv->x_server_process, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    }

    result = process_start (server->priv->x_server_process, FALSE);

    if (result)
        l_debug (display_server, "Waiting for ready signal from Xvnc server :%d", x_server_get_display_number (X_SERVER (server)));

    if (!result)
        stopped_cb (server->priv->x_server_process, X_SERVER_XVNC (server));

    return result;
}

static void
x_server_xvnc_stop (DisplayServer *server)
{
    process_stop (X_SERVER_XVNC (server)->priv->x_server_process);
}

static void
x_server_xvnc_init (XServerXVNC *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, X_SERVER_XVNC_TYPE, XServerXVNCPrivate);
    server->priv->command = g_strdup ("Xvnc");
    server->priv->width = 1024;
    server->priv->height = 768;
    server->priv->depth = 8;
}

static void
x_server_xvnc_finalize (GObject *object)
{
    XServerXVNC *self;

    self = X_SERVER_XVNC (object);

    if (self->priv->x_server_process)
        g_object_unref (self->priv->x_server_process);
    g_free (self->priv->command);
    g_free (self->priv->authority_file);

    G_OBJECT_CLASS (x_server_xvnc_parent_class)->finalize (object);
}

static void
x_server_xvnc_class_init (XServerXVNCClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->get_can_share = x_server_xvnc_get_can_share;
    display_server_class->start = x_server_xvnc_start;
    display_server_class->stop = x_server_xvnc_stop;
    object_class->finalize = x_server_xvnc_finalize;

    g_type_class_add_private (klass, sizeof (XServerXVNCPrivate));
}
