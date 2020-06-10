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
#include <stdlib.h>

#include "x-server-local.h"
#include "configuration.h"
#include "accounts.h"
#include "process.h"
#include "vt.h"

typedef struct
{
    /* X server process */
    Process *x_server_process;

    /* Command to run the X server */
    gchar *command;

    /* Optional user to drop privileges (switch) to */
    User *user;

    /* Display number to use */
    guint display_number;

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

    /* TRUE when received ready signal */
    gboolean got_signal;

    /* Poll source ID (fallback for signal if uids differ) */
    guint poll_for_socket_source;

    /* VT to run on */
    gint vt;
    gboolean have_vt_ref;

    /* Background to set */
    gchar *background;
} XServerLocalPrivate;

static void x_server_local_logger_iface_init (LoggerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (XServerLocal, x_server_local, X_SERVER_TYPE,
                         G_ADD_PRIVATE (XServerLocal)
                         G_IMPLEMENT_INTERFACE (LOGGER_TYPE, x_server_local_logger_iface_init))

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
    if (version)
        return version;

    g_autofree gchar *stderr_text = NULL;
    gint exit_status;
    if (!g_spawn_command_line_sync ("X -version", NULL, &stderr_text, &exit_status, NULL))
        return NULL;
    if (exit_status == EXIT_SUCCESS)
    {
        g_auto(GStrv) lines = g_strsplit (stderr_text, "\n", -1);
        for (int i = 0; lines[i] && !version; i++)
            version = find_version (lines[i]);
    }

    g_auto(GStrv) tokens = g_strsplit (version, ".", 3);
    guint n_tokens = g_strv_length (tokens);
    version_major = n_tokens > 0 ? atoi (tokens[0]) : 0;
    version_minor = n_tokens > 1 ? atoi (tokens[1]) : 0;

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
    /* See if we know we are managing a server with that number */
    for (GList *link = display_numbers; link; link = link->next)
    {
        guint number = GPOINTER_TO_UINT (link->data);
        if (number == display_number)
            return TRUE;
    }

    /* See if an X server that we don't know of has a lock on that number */
    g_autofree gchar *path = g_strdup_printf ("/tmp/.X%d-lock", display_number);
    gboolean in_use = g_file_test (path, G_FILE_TEST_EXISTS);

    /* See if that lock file is valid, ignore it if the contents are invalid or the process doesn't exist */
    g_autofree gchar *data = NULL;
    if (in_use && g_file_get_contents (path, &data, NULL, NULL))
    {
        int pid = atoi (g_strstrip (data));

        errno = 0;
        if (pid < 0 || (kill (pid, 0) < 0 && errno == ESRCH))
            in_use = FALSE;
    }

    return in_use;
}

guint
x_server_local_get_unused_display_number (void)
{
    guint number = config_get_integer (config_get_instance (), "LightDM", "minimum-display-number");
    while (display_number_in_use (number))
        number++;

    display_numbers = g_list_append (display_numbers, GUINT_TO_POINTER (number));

    return number;
}

void
x_server_local_release_display_number (guint display_number)
{
    for (GList *link = display_numbers; link; link = link->next)
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
    return g_object_new (X_SERVER_LOCAL_TYPE, NULL);
}

void
x_server_local_set_command (XServerLocal *server, const gchar *command)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->command);
    priv->command = g_strdup (command);
}

void
x_server_local_set_user(XServerLocal *server, User *user)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_return_if_fail (user != NULL);
    g_clear_object (&priv->user);
    priv->user = g_object_ref(user);
}

void
x_server_local_set_vt (XServerLocal *server, gint vt)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);

    g_return_if_fail (server != NULL);

    if (priv->have_vt_ref)
        vt_unref (priv->vt);
    priv->have_vt_ref = FALSE;
    priv->vt = vt;
    if (vt > 0)
    {
        vt_ref (vt);
        priv->have_vt_ref = TRUE;
    }
}

void
x_server_local_set_config (XServerLocal *server, const gchar *path)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->config_file);
    priv->config_file = g_strdup (path);
}

void
x_server_local_set_layout (XServerLocal *server, const gchar *layout)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->layout);
    priv->layout = g_strdup (layout);
}

void
x_server_local_set_xdg_seat (XServerLocal *server, const gchar *xdg_seat)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->xdg_seat);
    priv->xdg_seat = g_strdup (xdg_seat);
}

void
x_server_local_set_allow_tcp (XServerLocal *server, gboolean allow_tcp)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    priv->allow_tcp = allow_tcp;
}

void
x_server_local_set_xdmcp_server (XServerLocal *server, const gchar *hostname)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->xdmcp_server);
    priv->xdmcp_server = g_strdup (hostname);
}

const gchar *
x_server_local_get_xdmcp_server (XServerLocal *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_val_if_fail (server != NULL, 0);
    return priv->xdmcp_server;
}

void
x_server_local_set_xdmcp_port (XServerLocal *server, guint port)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    priv->xdmcp_port = port;
}

guint
x_server_local_get_xdmcp_port (XServerLocal *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_val_if_fail (server != NULL, 0);
    return priv->xdmcp_port;
}

void
x_server_local_set_xdmcp_key (XServerLocal *server, const gchar *key)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->xdmcp_key);
    priv->xdmcp_key = g_strdup (key);
    x_server_set_authority (X_SERVER (server), NULL);
}

void
x_server_local_set_background (XServerLocal *server, const gchar *background)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_if_fail (server != NULL);
    g_free (priv->background);
    priv->background = g_strdup (background);
}

static guint
x_server_local_get_display_number (XServer *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (X_SERVER_LOCAL (server));
    return priv->display_number;
}

static gint
x_server_local_get_vt (DisplayServer *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (X_SERVER_LOCAL (server));
    return priv->vt;
}

const gchar *
x_server_local_get_authority_file_path (XServerLocal *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    g_return_val_if_fail (server != NULL, 0);
    return priv->authority_file;
}

static gchar *
get_absolute_command (const gchar *command)
{
    g_auto(GStrv) tokens = g_strsplit (command, " ", 2);
    g_autofree gchar *absolute_binary = g_find_program_in_path (tokens[0]);
    gchar *absolute_command = NULL;
    if (absolute_binary)
    {
        if (tokens[1])
            absolute_command = g_strjoin (" ", absolute_binary, tokens[1], NULL);
        else
            absolute_command = g_strdup (absolute_binary);
    }

    return absolute_command;
}

static void
x_server_local_run (Process *process, gpointer user_data)
{
    /* Make input non-blocking */
    int fd = open ("/dev/null", O_RDONLY);
    dup2 (fd, STDIN_FILENO);
    close (fd);

    /* Set SIGUSR1 to ignore so the X server can indicate it when it is ready */
    signal (SIGUSR1, SIG_IGN);
}

static ProcessRunFunc
x_server_local_get_run_function (XServerLocal *server)
{
    return x_server_local_run;
}

static gboolean
x_server_local_get_log_stdout (XServerLocal *server)
{
    return TRUE;
}

static void
got_signal_cb (Process *process, int signum, XServerLocal *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);

    if (signum == SIGUSR1 && !priv->got_signal)
    {
        priv->got_signal = TRUE;
        l_debug (server, "Got signal from X server :%d", priv->display_number);

        // FIXME: Check return value
        DISPLAY_SERVER_CLASS (x_server_local_parent_class)->start (DISPLAY_SERVER (server));
    }
}

static gboolean
poll_for_socket_cb (XServerLocal *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);

    /* Check is X11 socket file exists as an alternative startup test to SIGUSR1 */
    GStatBuf statbuf;
    g_autofree gchar *socketpath = g_strdup_printf ("/tmp/.X11-unix/X%d", priv->display_number);
    if ( g_stat (socketpath, &statbuf) == 0 )
    {
        uid_t uid = priv->user ? user_get_uid(priv->user) : 0;

        /* It has to be a valid socket file */
        if (!(statbuf.st_mode & S_IFSOCK))
        {
            l_debug (server, "X11 socket file is not a socket: %s", socketpath);
            return G_SOURCE_REMOVE;
        }

        /* It has to be owned by the correct user */
        if (statbuf.st_uid != uid)
        {
            l_debug (server, "X11 socket file is not owned by uid %d: %s", uid, socketpath);
            return G_SOURCE_REMOVE;
        }

        /* Consider SIGUSR1 to have been recieved */
        priv->got_signal = TRUE;
        l_debug (server, "Detected valid X11 socket for X server :%d", priv->display_number);

        // FIXME: Check return value
        DISPLAY_SERVER_CLASS (x_server_local_parent_class)->start (DISPLAY_SERVER (server));

        return G_SOURCE_REMOVE;
    }

    /* Wait another second and check again */
    return G_SOURCE_CONTINUE;
}

static void
stopped_cb (Process *process, XServerLocal *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);

    l_debug (server, "X server stopped");

    /* Release VT and display number for re-use */
    if (priv->have_vt_ref)
    {
        vt_unref (priv->vt);
        priv->have_vt_ref = FALSE;
    }
    x_server_local_release_display_number (priv->display_number);

    if (x_server_get_authority (X_SERVER (server)) && priv->authority_file)
    {
        l_debug (server, "Removing X server authority %s", priv->authority_file);

        g_unlink (priv->authority_file);

        g_free (priv->authority_file);
        priv->authority_file = NULL;
    }

    DISPLAY_SERVER_CLASS (x_server_local_parent_class)->stop (DISPLAY_SERVER (server));
}

static void
write_authority_file (XServerLocal *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);

    XAuthority *authority = x_server_get_authority (X_SERVER (server));
    g_return_if_fail (authority != NULL);

    /* Get file to write to if have authority */
    if (priv->authority_file == NULL)
    {
        g_autofree gchar *run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
        g_autofree gchar *dir = g_build_filename (run_dir, priv->user ? user_get_name (priv->user) : "root", NULL);

        if (g_mkdir_with_parents (dir, S_IRWXU) < 0)
            l_warning (server, "Failed to make authority directory %s: %s", dir, strerror (errno));
        if (priv->user != NULL && getuid () == 0)
            if (chown (dir, user_get_uid (priv->user), user_get_gid (priv->user)) < 0)
                l_warning (server, "Failed to set ownership of x-server authority dir: %s", strerror (errno));

        priv->authority_file = g_build_filename (dir, x_server_get_address (X_SERVER (server)), NULL);
    }

    l_debug (server, "Writing X server authority to %s", priv->authority_file);

    g_autoptr(GError) error = NULL;
    x_authority_write (authority, XAUTH_WRITE_MODE_REPLACE, priv->authority_file, &error);
    if (error)
        l_warning (server, "Failed to write authority: %s", error->message);
    if (priv->user != NULL && getuid () == 0)
      if (chown (priv->authority_file, user_get_uid (priv->user), user_get_gid (priv->user)) < 0)
            l_warning (server, "Failed to set ownership of authority: %s", strerror (errno));
}

static gboolean
x_server_local_start (DisplayServer *display_server)
{
    XServerLocal *server = X_SERVER_LOCAL (display_server);
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);

    g_return_val_if_fail (priv->x_server_process == NULL, FALSE);

    priv->got_signal = FALSE;

    g_return_val_if_fail (priv->command != NULL, FALSE);

    ProcessRunFunc run_cb = X_SERVER_LOCAL_GET_CLASS (server)->get_run_function (server);
    priv->x_server_process = process_new (run_cb, server);
    process_set_clear_environment (priv->x_server_process, TRUE);
    if (priv->user == NULL || user_get_uid (priv->user) == getuid ())
        g_signal_connect (priv->x_server_process, PROCESS_SIGNAL_GOT_SIGNAL, G_CALLBACK (got_signal_cb), server);
    else if (!priv->poll_for_socket_source)
        priv->poll_for_socket_source = g_timeout_add_seconds (1, (GSourceFunc)poll_for_socket_cb, server);
    g_signal_connect (priv->x_server_process, PROCESS_SIGNAL_STOPPED, G_CALLBACK (stopped_cb), server);

    /* Setup logging */
    g_autofree gchar *filename = g_strdup_printf ("x-%d.log", x_server_get_display_number (X_SERVER (server)));
    g_autofree gchar *dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    g_autofree gchar *log_file = g_build_filename (dir, filename, NULL);
    gboolean backup_logs = config_get_boolean (config_get_instance (), "LightDM", "backup-logs");
    process_set_log_file (priv->x_server_process, log_file, X_SERVER_LOCAL_GET_CLASS (server)->get_log_stdout (server), backup_logs ? LOG_MODE_BACKUP_AND_TRUNCATE : LOG_MODE_APPEND);
    l_debug (display_server, "Logging to %s", log_file);

    g_autofree gchar *absolute_command = get_absolute_command (priv->command);
    if (!absolute_command)
    {
        l_debug (display_server, "Can't launch X server %s, not found in path", priv->command);
        stopped_cb (priv->x_server_process, X_SERVER_LOCAL (server));
        return FALSE;
    }
    g_autoptr(GString) command = g_string_new (absolute_command);

    g_string_append_printf (command, " :%d", priv->display_number);

    if (priv->config_file)
        g_string_append_printf (command, " -config %s", priv->config_file);

    if (priv->layout)
        g_string_append_printf (command, " -layout %s", priv->layout);

    if (priv->xdg_seat)
        g_string_append_printf (command, " -seat %s", priv->xdg_seat);

    write_authority_file (server);
    if (priv->authority_file)
        g_string_append_printf (command, " -auth %s", priv->authority_file);

    /* Connect to a remote server using XDMCP */
    if (priv->xdmcp_server != NULL)
    {
        if (priv->xdmcp_port != 0)
            g_string_append_printf (command, " -port %d", priv->xdmcp_port);
        g_string_append_printf (command, " -query %s", priv->xdmcp_server);
        if (priv->xdmcp_key)
            g_string_append_printf (command, " -cookie %s", priv->xdmcp_key);
    }
    else if (priv->allow_tcp)
    {
        if (x_server_local_version_compare (1, 17) >= 0)
            g_string_append (command, " -listen tcp");
    }
    else
        g_string_append (command, " -nolisten tcp");

    if (priv->vt >= 0)
        g_string_append_printf (command, " vt%d -novtswitch", priv->vt);

    if (priv->background)
        g_string_append_printf (command, " -background %s", priv->background);

    /* Allow sub-classes to add arguments */
    if (X_SERVER_LOCAL_GET_CLASS (server)->add_args)
        X_SERVER_LOCAL_GET_CLASS (server)->add_args (server, command);

    process_set_command (priv->x_server_process, command->str);
    if (priv->user)
        process_set_user (priv->x_server_process, priv->user);

    l_debug (display_server, "Launching X Server");

    /* If running inside another display then pass through those variables */
    if (g_getenv ("DISPLAY"))
    {
        process_set_env (priv->x_server_process, "DISPLAY", g_getenv ("DISPLAY"));
        if (g_getenv ("XAUTHORITY"))
            process_set_env (priv->x_server_process, "XAUTHORITY", g_getenv ("XAUTHORITY"));
        else
        {
            g_autofree gchar *path = g_build_filename (g_get_home_dir (), ".Xauthority", NULL);
            process_set_env (priv->x_server_process, "XAUTHORITY", path);
        }
    }

    /* Pass through library variables */
    if (g_getenv ("LD_PRELOAD"))
        process_set_env (priv->x_server_process, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
    if (g_getenv ("LD_LIBRARY_PATH"))
        process_set_env (priv->x_server_process, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    if (g_getenv ("PATH"))
        process_set_env (priv->x_server_process, "PATH", g_getenv ("PATH"));

    /* Variable required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
        process_set_env (priv->x_server_process, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));

    gboolean result = process_start (priv->x_server_process, FALSE);
    if (result)
        l_debug (display_server, "Waiting for ready signal from X server :%d", priv->display_number);
    else
        stopped_cb (priv->x_server_process, X_SERVER_LOCAL (server));

    return result;
}

static void
x_server_local_stop (DisplayServer *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (X_SERVER_LOCAL (server));
    process_stop (priv->x_server_process);
}

static void
x_server_local_init (XServerLocal *server)
{
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    priv->vt = -1;
    priv->command = g_strdup ("X");
    priv->display_number = x_server_local_get_unused_display_number ();
}

static void
x_server_local_finalize (GObject *object)
{
    XServerLocal *self = X_SERVER_LOCAL (object);
    XServerLocalPrivate *priv = x_server_local_get_instance_private (self);

    if (priv->x_server_process)
        g_signal_handlers_disconnect_matched (priv->x_server_process, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    if (priv->poll_for_socket_source)
    {
        g_source_remove (priv->poll_for_socket_source);
        priv->poll_for_socket_source = 0;
    }
    g_clear_object (&priv->x_server_process);
    g_clear_pointer (&priv->command, g_free);
    g_clear_object (&priv->user);
    g_clear_pointer (&priv->config_file, g_free);
    g_clear_pointer (&priv->layout, g_free);
    g_clear_pointer (&priv->xdg_seat, g_free);
    g_clear_pointer (&priv->xdmcp_server, g_free);
    g_clear_pointer (&priv->xdmcp_key, g_free);
    g_clear_pointer (&priv->authority_file, g_free);
    if (priv->have_vt_ref)
        vt_unref (priv->vt);
    g_clear_pointer (&priv->background, g_free);

    G_OBJECT_CLASS (x_server_local_parent_class)->finalize (object);
}

static void
x_server_local_class_init (XServerLocalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    XServerClass *x_server_class = X_SERVER_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    klass->get_run_function = x_server_local_get_run_function;
    klass->get_log_stdout = x_server_local_get_log_stdout;
    x_server_class->get_display_number = x_server_local_get_display_number;
    display_server_class->get_vt = x_server_local_get_vt;
    display_server_class->start = klass->start = x_server_local_start;
    display_server_class->stop = x_server_local_stop;
    object_class->finalize = x_server_local_finalize;
}

static gint
x_server_local_real_logprefix (Logger *self, gchar *buf, gulong buflen)
{
    XServerLocal *server = X_SERVER_LOCAL (self);
    XServerLocalPrivate *priv = x_server_local_get_instance_private (server);
    return g_snprintf (buf, buflen, "XServer %d: ", priv->display_number);
}

static void
x_server_local_logger_iface_init (LoggerInterface *iface)
{
    iface->logprefix = &x_server_local_real_logprefix;
}
