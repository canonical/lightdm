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


#include "xserver-local.h"
#include "configuration.h"
#include "child-process.h"
#include "vt.h"
#include "plymouth.h"

struct XServerLocalPrivate
{
    /* X server process */
    ChildProcess *xserver_process;

    /* Path of file to log to */
    gchar *log_file;

    /* Command to run the X server */
    gchar *command;

    /* Config file to use */
    gchar *config_file;

    /* Server layout to use */
    gchar *layout;

    /* XDMCP server to connect to */
    gchar *xdmcp_server;

    /* XDMCP port to connect to */
    guint xdmcp_port;

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
xserver_local_new (const gchar *config_section)
{
    XServerLocal *self = g_object_new (XSERVER_LOCAL_TYPE, NULL);

    xserver_set_display_number (XSERVER (self), get_free_display_number ());

    /* If running inside an X server use Xephyr instead */
    if (g_getenv ("DISPLAY"))
        self->priv->command = g_strdup ("Xephyr");
    if (!self->priv->command && config_section)
        self->priv->command = config_get_string (config_get_instance (), config_section, "xserver-command");
    if (!self->priv->command)
        self->priv->command = config_get_string (config_get_instance (), "SeatDefaults", "xserver-command");

    if (config_section)
        self->priv->layout = config_get_string (config_get_instance (), config_section, "xserver-layout");
    if (!self->priv->layout)
        self->priv->layout = config_get_string (config_get_instance (), "SeatDefaults", "layout");

    if (config_section)
        self->priv->config_file = config_get_string (config_get_instance (), config_section, "xserver-config");
    if (!self->priv->config_file)
        self->priv->config_file = config_get_string (config_get_instance (), "SeatDefaults", "xserver-config");
  
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
got_signal_cb (ChildProcess *process, int signum, XServerLocal *server)
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

        xserver_connect (XSERVER (server));
    }
}

static void
exit_cb (ChildProcess *process, int status, XServerLocal *server)
{
    if (status != 0)
        g_debug ("X server exited with value %d", status);
}

static void
terminated_cb (ChildProcess *process, int signum, XServerLocal *server)
{
    g_debug ("X server terminated with signal %d", signum);
}

static void
stopped_cb (ChildProcess *process, XServerLocal *server)
{
    g_debug ("stopped");

    g_object_unref (server->priv->xserver_process);
    server->priv->xserver_process = NULL;

    if (server->priv->replacing_plymouth && plymouth_get_is_running ())
    {
        g_debug ("Stopping Plymouth, X server failed to start");
        server->priv->replacing_plymouth = FALSE;
        plymouth_quit (FALSE);
    }

    xserver_disconnect (XSERVER (server));
}

static gboolean
xserver_local_start (XServer *server)
{
    GError *error = NULL;
    gboolean result;
    gchar *filename, *dir, *path, *absolute_command;
    GFile *auth_file;
    GString *command;

    g_return_val_if_fail (XSERVER_LOCAL (server)->priv->xserver_process == NULL, FALSE);

    XSERVER_LOCAL (server)->priv->got_signal = FALSE;

    g_return_val_if_fail (XSERVER_LOCAL (server)->priv->command != NULL, FALSE);

    XSERVER_LOCAL (server)->priv->xserver_process = child_process_new ();
    g_signal_connect (XSERVER_LOCAL (server)->priv->xserver_process, "got-signal", G_CALLBACK (got_signal_cb), server);
    g_signal_connect (XSERVER_LOCAL (server)->priv->xserver_process, "exited", G_CALLBACK (exit_cb), server);
    g_signal_connect (XSERVER_LOCAL (server)->priv->xserver_process, "terminated", G_CALLBACK (terminated_cb), server);
    g_signal_connect (XSERVER_LOCAL (server)->priv->xserver_process, "stopped", G_CALLBACK (stopped_cb), server);

    /* Setup logging */
    filename = g_strdup_printf ("%s.log", xserver_get_address (XSERVER (server)));
    dir = config_get_string (config_get_instance (), "Directories", "log-directory");
    path = g_build_filename (dir, filename, NULL);
    g_debug ("Logging to %s", path);
    child_process_set_log_file (XSERVER_LOCAL (server)->priv->xserver_process, path);
    g_free (filename);
    g_free (dir);
    g_free (path);

    absolute_command = get_absolute_command (XSERVER_LOCAL (server)->priv->command);
    if (!absolute_command)
    {
        g_debug ("Can't launch X server %s, not found in path", XSERVER_LOCAL (server)->priv->command);
        stopped_cb (XSERVER_LOCAL (server)->priv->xserver_process, XSERVER_LOCAL (server));
        return FALSE;
    }
    command = g_string_new (absolute_command);
    g_free (absolute_command);

    g_string_append_printf (command, " :%d", xserver_get_display_number (server));

    if (XSERVER_LOCAL (server)->priv->config_file)
        g_string_append_printf (command, " -config %s", XSERVER_LOCAL (server)->priv->config_file);

    if (XSERVER_LOCAL (server)->priv->layout)
        g_string_append_printf (command, " -layout %s", XSERVER_LOCAL (server)->priv->layout);

    auth_file = xserver_get_authority_file (server);
    if (auth_file)
    {
        gchar *path = g_file_get_path (auth_file);
        g_string_append_printf (command, " -auth %s", path);
        g_free (path);
    }

    /* Connect to a remote server using XDMCP */
    if (XSERVER_LOCAL (server)->priv->xdmcp_server != NULL)
    {
        if (XSERVER_LOCAL (server)->priv->xdmcp_port != 0)
            g_string_append_printf (command, " -port %d", XSERVER_LOCAL (server)->priv->xdmcp_port);
        g_string_append_printf (command, " -query %s", XSERVER_LOCAL (server)->priv->xdmcp_server);
        if (g_strcmp0 (xserver_get_authentication_name (server), "XDM-AUTHENTICATION-1") == 0)
        {
            GString *cookie;
            const guint8 *data;
            gsize data_length, i;

            data = xserver_get_authentication_data (server);
            data_length = xserver_get_authentication_data_length (server);
            cookie = g_string_new ("0x");
            for (i = 0; i < data_length; i++)
                g_string_append_printf (cookie, "%02X", data[i]);
            g_string_append_printf (command, " -cookie %s", cookie->str);
            g_string_free (cookie, TRUE);
        }
    }
    else
        g_string_append (command, " -nolisten tcp");

    if (XSERVER_LOCAL (server)->priv->vt >= 0)
        g_string_append_printf (command, " vt%d", XSERVER_LOCAL (server)->priv->vt);

    if (XSERVER_LOCAL (server)->priv->replacing_plymouth)
        g_string_append (command, " -background none");

    g_debug ("Launching X Server");

    /* If running inside another display then pass through those variables */
    if (g_getenv ("DISPLAY"))
        child_process_set_env (XSERVER_LOCAL (server)->priv->xserver_process, "DISPLAY", g_getenv ("DISPLAY"));
    if (g_getenv ("XAUTHORITY"))
        child_process_set_env (XSERVER_LOCAL (server)->priv->xserver_process, "XAUTHORITY", g_getenv ("XAUTHORITY"));

    /* Variable required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"))
    {
        child_process_set_env (XSERVER_LOCAL (server)->priv->xserver_process, "LIGHTDM_TEST_STATUS_SOCKET", g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"));
        child_process_set_env (XSERVER_LOCAL (server)->priv->xserver_process, "LIGHTDM_TEST_CONFIG", g_getenv ("LIGHTDM_TEST_CONFIG"));
        child_process_set_env (XSERVER_LOCAL (server)->priv->xserver_process, "LIGHTDM_TEST_HOME_DIR", g_getenv ("LIGHTDM_TEST_HOME_DIR"));
        child_process_set_env (XSERVER_LOCAL (server)->priv->xserver_process, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    }

    result = child_process_start (XSERVER_LOCAL (server)->priv->xserver_process,
                                  user_get_current (),
                                  NULL,
                                  command->str,
                                  FALSE,
                                  &error);
    g_string_free (command, TRUE);
    if (!result)
        g_warning ("Unable to create display: %s", error->message);
    else
        g_debug ("Waiting for ready signal from X server :%d", xserver_get_display_number (server));
    g_clear_error (&error);

    if (!result)
        stopped_cb (XSERVER_LOCAL (server)->priv->xserver_process, XSERVER_LOCAL (server));

    return result;
}
 
static gboolean
xserver_local_restart (XServer *server)
{
    /* Not running */
    if (!XSERVER_LOCAL (server)->priv->xserver_process)
        return FALSE;

    g_debug ("Sending signal to X server to disconnect clients");

    XSERVER_LOCAL (server)->priv->got_signal = FALSE;
    child_process_signal (XSERVER_LOCAL (server)->priv->xserver_process, SIGHUP);

    return TRUE;
}

static void
xserver_local_stop (XServer *server)
{
    child_process_stop (XSERVER_LOCAL (server)->priv->xserver_process);
}

static void
xserver_local_init (XServerLocal *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_LOCAL_TYPE, XServerLocalPrivate);
    server->priv->vt = -1;
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

    G_OBJECT_CLASS (xserver_local_parent_class)->finalize (object);
}

static void
xserver_local_class_init (XServerLocalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    XServerClass *xserver_class = XSERVER_CLASS (klass);

    xserver_class->start = xserver_local_start;
    xserver_class->restart = xserver_local_restart;
    xserver_class->stop = xserver_local_stop;
    object_class->finalize = xserver_local_finalize;

    g_type_class_add_private (klass, sizeof (XServerLocalPrivate));
}
