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
#include <stdlib.h>

#include "x-server-local.h"
#include "configuration.h"
#include "process.h"
#include "vt.h"

struct XServerLocalPrivate
{
    /* X server process */
    Process *x_server_process;

    /* Command to run the X server */
    gchar *command;

    /* Config file to use */
    gchar *config_file;

    /* Server layout to use */
    gchar *layout;

    /* Value for -seat argument */
    gchar *xdg_seat;

    /* TRUE if TCP/IP connections are allowed */
    gboolean allow_tcp;

    /* Authority file */
    gchar *authority_file;

    /* XDMCP server to connect to */
    gchar *xdmcp_server;

    /* XDMCP port to connect to */
    guint xdmcp_port;

    /* XDMCP key to use */
    gchar *xdmcp_key;

    /* ID to report to Mir */
    gchar *mir_id;

    /* Filename of socket Mir is listening on */
    gchar *mir_socket;

    /* TRUE when received ready signal */
    gboolean got_signal;

    /* VT to run on */
    gint vt;
    gboolean have_vt_ref;

    /* Background to set */
    gchar *background;
};

G_DEFINE_TYPE (XServerLocal, x_server_local, X_SERVER_TYPE);

static gchar *version = NULL;
static guint version_major = 0, version_minor = 0;
static GList *display_numbers = NULL;

#define XORG_VERSION_PREFIX "X.Org X Server "

static gchar *
find_version (const gchar *line)
{
    if (!g_str_has_prefix (line, XORG_VERSION_PREFIX))
        return NULL;

    return g_strdup (line + strlen (XORG_VERSION_PREFIX));
}

const gchar *
x_server_local_get_version (void)
{
    gchar *stderr_text;
    gint exit_status;
    gchar **tokens;
    guint n_tokens;

    if (version)
        return version;

    if (!g_spawn_command_line_sync ("X -version", NULL, &stderr_text, &exit_status, NULL))
        return NULL;
    if (exit_status == EXIT_SUCCESS)
    {
        gchar **lines;
        int i;

        lines = g_strsplit (stderr_text, "\n", -1);
        for (i = 0; lines[i] && !version; i++)
            version = find_version (lines[i]);
        g_strfreev (lines);
    }
    g_free (stderr_text);

    tokens = g_strsplit (version, ".", 3);
    n_tokens = g_strv_length (tokens);
    version_major = n_tokens > 0 ? atoi (tokens[0]) : 0;
    version_minor = n_tokens > 1 ? atoi (tokens[1]) : 0;
    g_strfreev (tokens);

    return version;
}

gint
x_server_local_version_compare (guint major, guint minor)
{
    x_server_local_get_version ();
    if (major == version_major)
        return version_minor - minor;
    else
        return version_major - major;
}

static gboolean
display_number_in_use (guint display_number)
{
    GList *link;
    gchar *path;
    gboolean in_use;
    gchar *data;

    /* See if we know we are managing a server with that number */
    for (link = display_numbers; link; link = link->next)
    {
        guint number = GPOINTER_TO_UINT (link->data);
        if (number == display_number)
            return TRUE;
    }

    /* See if an X server that we don't know of has a lock on that number */
    path = g_strdup_printf ("/tmp/.X%d-lock", display_number);
    in_use = g_file_test (path, G_FILE_TEST_EXISTS);

    /* See if that lock file is valid, ignore it if the contents are invalid or the process doesn't exist */
    if (in_use && g_file_get_contents (path, &data, NULL, NULL))
    {
        int pid;

        pid = atoi (g_strstrip (data));
        g_free (data);

        errno = 0;
        if (pid < 0 || (kill (pid, 0) < 0 && errno == ESRCH))
            in_use = FALSE;
    }

    g_free (path);

    return in_use;
}

guint
x_server_local_get_unused_display_number (void)
{
    guint number;

    number = config_get_integer (config_get_instance (), "LightDM", "minimum-display-number");
    while (display_number_in_use (number))
        number++;

    display_numbers = g_list_append (display_numbers, GUINT_TO_POINTER (number));

    return number;
}

void
x_server_local_release_display_number (guint display_number)
{
    GList *link;
    for (link = display_numbers; link; link = link->next)
    {
        guint number = GPOINTER_TO_UINT (link->data);
        if (number == display_number)
        {
            display_numbers = g_list_delete_link (display_numbers, link);
            return;
        }
    }
}

XServerLocal *
x_server_local_new (void)
{
    XServerLocal *self;
    gchar hostname[1024], *number, *name;
    XAuthority *cookie;

    self = g_object_new (X_SERVER_LOCAL_TYPE, NULL);

    x_server_set_display_number (X_SERVER (self), x_server_local_get_unused_display_number ());

    gethostname (hostname, 1024);
    number = g_strdup_printf ("%d", x_server_get_display_number (X_SERVER (self)));
    cookie = x_authority_new_cookie (XAUTH_FAMILY_LOCAL, (guint8*) hostname, strlen (hostname), number);
    x_server_set_authority (X_SERVER (self), cookie);
    g_free (number);
    g_object_unref (cookie);

    name = g_strdup_printf ("x-%d", x_server_get_display_number (X_SERVER (self)));
    display_server_set_name (DISPLAY_SERVER (self), name);
    g_free (name);

    return self;
}

void
x_server_local_set_command (XServerLocal *server, const gchar *command)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->command);
    server->priv->command = g_strdup (command);
}

void
x_server_local_set_vt (XServerLocal *server, gint vt)
{
    g_return_if_fail (server != NULL);
    if (server->priv->have_vt_ref)
        vt_unref (server->priv->vt);
    server->priv->have_vt_ref = FALSE;
    server->priv->vt = vt;
    if (vt > 0)
    {
        vt_ref (vt);
        server->priv->have_vt_ref = TRUE;
    }
}

void
x_server_local_set_config (XServerLocal *server, const gchar *path)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->config_file);
    server->priv->config_file = g_strdup (path);
}

void
x_server_local_set_layout (XServerLocal *server, const gchar *layout)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->layout);
    server->priv->layout = g_strdup (layout);
}

void
x_server_local_set_xdg_seat (XServerLocal *server, const gchar *xdg_seat)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->xdg_seat);
    server->priv->xdg_seat = g_strdup (xdg_seat);
}

void
x_server_local_set_allow_tcp (XServerLocal *server, gboolean allow_tcp)
{
    g_return_if_fail (server != NULL);
    server->priv->allow_tcp = allow_tcp;
}

void
x_server_local_set_xdmcp_server (XServerLocal *server, const gchar *hostname)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->xdmcp_server);
    server->priv->xdmcp_server = g_strdup (hostname);
}

const gchar *
x_server_local_get_xdmcp_server (XServerLocal *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->xdmcp_server;
}

void
x_server_local_set_xdmcp_port (XServerLocal *server, guint port)
{
    g_return_if_fail (server != NULL);
    server->priv->xdmcp_port = port;
}

guint
x_server_local_get_xdmcp_port (XServerLocal *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->xdmcp_port;
}

void
x_server_local_set_xdmcp_key (XServerLocal *server, const gchar *key)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->xdmcp_key);
    server->priv->xdmcp_key = g_strdup (key);
    x_server_set_authority (X_SERVER (server), NULL);
}

void
x_server_local_set_background (XServerLocal *server, const gchar *background)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->background);
    server->priv->background = g_strdup (background);
}

void
x_server_local_set_mir_id (XServerLocal *server, const gchar *id)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->mir_id);
    server->priv->mir_id = g_strdup (id);
}

const gchar *x_server_local_get_mir_id (XServerLocal *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->mir_id;
}

void
x_server_local_set_mir_socket (XServerLocal *server, const gchar *socket)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->mir_socket);
    server->priv->mir_socket = g_strdup (socket);
}

static gint
x_server_local_get_vt (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return X_SERVER_LOCAL (server)->priv->vt;
}

const gchar *
x_server_local_get_authority_file_path (XServerLocal *server)
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
    g_free (absolute_binary);

    g_strfreev (tokens);

    return absolute_command;
}

static void
run_cb (Process *process, gpointer user_data)
{
    int fd;

    /* Make input non-blocking */
    fd = open ("/dev/null", O_RDONLY);
    dup2 (fd, STDIN_FILENO);
    close (fd);

    /* Set SIGUSR1 to ignore so the X server can indicate it when it is ready */
    signal (SIGUSR1, SIG_IGN);
}

static void
got_signal_cb (Process *process, int signum, XServerLocal *server)
{
    if (signum == SIGUSR1 && !server->priv->got_signal)
    {
        server->priv->got_signal = TRUE;
        l_debug (server, "Got signal from X server :%d", x_server_get_display_number (X_SERVER (server)));

        // FIXME: Check return value
        DISPLAY_SERVER_CLASS (x_server_local_parent_class)->start (DISPLAY_SERVER (server));
    }
}

static void
stopped_cb (Process *process, XServerLocal *server)
{
    l_debug (server, "X server stopped");

    /* Release VT and display number for re-use */
    if (server->priv->have_vt_ref)
    {
        vt_unref (server->priv->vt);
        server->priv->have_vt_ref = FALSE;
    }
    x_server_local_release_display_number (x_server_get_display_number (X_SERVER (server)));

    if (x_server_get_authority (X_SERVER (server)) && server->priv->authority_file)
    {
        l_debug (server, "Removing X server authority %s", server->priv->authority_file);

        g_unlink (server->priv->authority_file);

        g_free (server->priv->authority_file);
        server->priv->authority_file = NULL;
    }

    DISPLAY_SERVER_CLASS (x_server_local_parent_class)->stop (DISPLAY_SERVER (server));
}

static void
write_authority_file (XServerLocal *server)
{
    XAuthority *authority;
    GError *error = NULL;

    authority = x_server_get_authority (X_SERVER (server));
    if (!authority)
        return;

    /* Get file to write to if have authority */
    if (!server->priv->authority_file)
    {
        gchar *run_dir, *dir;

        run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
        dir = g_build_filename (run_dir, "root", NULL);
        g_free (run_dir);
        if (g_mkdir_with_parents (dir, S_IRWXU) < 0)
            l_warning (server, "Failed to make authority directory %s: %s", dir, strerror (errno));

        server->priv->authority_file = g_build_filename (dir, x_server_get_address (X_SERVER (server)), NULL);
        g_free (dir);
    }

    l_debug (server, "Writing X server authority to %s", server->priv->authority_file);

    x_authority_write (authority, XAUTH_WRITE_MODE_REPLACE, server->priv->authority_file, &error);
    if (error)
        l_warning (server, "Failed to write authority: %s", error->message);
    g_clear_error (&error);
}

static gboolean
x_server_local_start (DisplayServer *display_server)
{
    XServerLocal *server = X_SERVER_LOCAL (display_server);
    gboolean result, backup_logs;
    gchar *filename, *dir, *log_file, *absolute_command;
    GString *command;

    g_return_val_if_fail (server->priv->x_server_process == NULL, FALSE);

    server->priv->got_signal = FALSE;

    g_return_val_if_fail (server->priv->command != NULL, FALSE);

    server->priv->x_server_process = process_new (run_cb, server);
    process_set_clear_environment (server->priv->x_server_process, TRUE);
    g_signal_connect (server->priv->x_server_process, PROCESS_SIGNAL_GOT_SIGNAL, G_CALLBACK (got_signal_cb), server);
    g_signal_connect (server->priv->x_server_process, PROCESS_SIGNAL_STOPPED, G_CALLBACK (stopped_cb), server);

    /* Setup logging */
    filename = g_strdup_printf ("%s.log", display_server_get_name (display_server));
    dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    log_file = g_build_filename (dir, filename, NULL);
    backup_logs = config_get_boolean (config_get_instance (), "LightDM", "backup-logs");
    process_set_log_file (server->priv->x_server_process, log_file, TRUE, backup_logs ? LOG_MODE_BACKUP_AND_TRUNCATE : LOG_MODE_APPEND);
    l_debug (display_server, "Logging to %s", log_file);
    g_free (log_file);
    g_free (filename);
    g_free (dir);

    absolute_command = get_absolute_command (server->priv->command);
    if (!absolute_command)
    {
        l_debug (display_server, "Can't launch X server %s, not found in path", server->priv->command);
        stopped_cb (server->priv->x_server_process, X_SERVER_LOCAL (server));
        return FALSE;
    }
    command = g_string_new (absolute_command);
    g_free (absolute_command);

    g_string_append_printf (command, " :%d", x_server_get_display_number (X_SERVER (server)));

    if (server->priv->config_file)
        g_string_append_printf (command, " -config %s", server->priv->config_file);

    if (server->priv->layout)
        g_string_append_printf (command, " -layout %s", server->priv->layout);

    if (server->priv->xdg_seat)
        g_string_append_printf (command, " -seat %s", server->priv->xdg_seat);

    write_authority_file (server);
    if (server->priv->authority_file)
        g_string_append_printf (command, " -auth %s", server->priv->authority_file);

    /* Setup for running inside Mir */
    if (server->priv->mir_id)
        g_string_append_printf (command, " -mir %s", server->priv->mir_id);

    if (server->priv->mir_socket)
        g_string_append_printf (command, " -mirSocket %s", server->priv->mir_socket);

    /* Connect to a remote server using XDMCP */
    if (server->priv->xdmcp_server != NULL)
    {
        if (server->priv->xdmcp_port != 0)
            g_string_append_printf (command, " -port %d", server->priv->xdmcp_port);
        g_string_append_printf (command, " -query %s", server->priv->xdmcp_server);
        if (server->priv->xdmcp_key)
            g_string_append_printf (command, " -cookie %s", server->priv->xdmcp_key);
    }
    else if (server->priv->allow_tcp)
    {
        if (x_server_local_version_compare (1, 17) >= 0)
            g_string_append (command, " -listen tcp");
    }
    else
        g_string_append (command, " -nolisten tcp");

    if (server->priv->vt >= 0)
        g_string_append_printf (command, " vt%d -novtswitch", server->priv->vt);

    if (server->priv->background)
        g_string_append_printf (command, " -background %s", server->priv->background);

    process_set_command (server->priv->x_server_process, command->str);
    g_string_free (command, TRUE);

    l_debug (display_server, "Launching X Server");

    /* If running inside another display then pass through those variables */
    if (g_getenv ("DISPLAY"))
    {
        process_set_env (server->priv->x_server_process, "DISPLAY", g_getenv ("DISPLAY"));
        if (g_getenv ("XAUTHORITY"))
            process_set_env (server->priv->x_server_process, "XAUTHORITY", g_getenv ("XAUTHORITY"));
        else
        {
            gchar *path;
            path = g_build_filename (g_get_home_dir (), ".Xauthority", NULL);
            process_set_env (server->priv->x_server_process, "XAUTHORITY", path);
            g_free (path);
        }
    }

    /* Variable required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
    {
        process_set_env (server->priv->x_server_process, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));
        process_set_env (server->priv->x_server_process, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
        process_set_env (server->priv->x_server_process, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    }

    result = process_start (server->priv->x_server_process, FALSE);

    if (result)
        l_debug (display_server, "Waiting for ready signal from X server :%d", x_server_get_display_number (X_SERVER (server)));

    if (!result)
        stopped_cb (server->priv->x_server_process, X_SERVER_LOCAL (server));

    return result;
}

static void
x_server_local_stop (DisplayServer *server)
{
    process_stop (X_SERVER_LOCAL (server)->priv->x_server_process);
}

static void
x_server_local_init (XServerLocal *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, X_SERVER_LOCAL_TYPE, XServerLocalPrivate);
    server->priv->vt = -1;
    server->priv->command = g_strdup ("X");
}

static void
x_server_local_finalize (GObject *object)
{
    XServerLocal *self = X_SERVER_LOCAL (object);

    if (self->priv->x_server_process)
    {
        g_signal_handlers_disconnect_matched (self->priv->x_server_process, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
        g_object_unref (self->priv->x_server_process);
    }
    g_free (self->priv->command);
    g_free (self->priv->config_file);
    g_free (self->priv->layout);
    g_free (self->priv->xdg_seat);
    g_free (self->priv->xdmcp_server);
    g_free (self->priv->xdmcp_key);
    g_free (self->priv->mir_id);
    g_free (self->priv->mir_socket);
    g_free (self->priv->authority_file);
    if (self->priv->have_vt_ref)
        vt_unref (self->priv->vt);
    g_free (self->priv->background);

    G_OBJECT_CLASS (x_server_local_parent_class)->finalize (object);
}

static void
x_server_local_class_init (XServerLocalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->get_vt = x_server_local_get_vt;
    display_server_class->start = x_server_local_start;
    display_server_class->stop = x_server_local_stop;
    object_class->finalize = x_server_local_finalize;

    g_type_class_add_private (klass, sizeof (XServerLocalPrivate));
}
