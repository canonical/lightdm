/*
 * Copyright (C) 2012 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <fcntl.h>
#include <errno.h>
#include <mir_client_library_lightdm.h>

#include "seat-mir.h"
#include "configuration.h"
#include "xserver-local.h"
#include "xsession.h"
#include "vt.h"
#include "plymouth.h"

struct SeatMirPrivate
{
    /* VT we are running on */
    gint vt;

    /* TRUE if waiting for X server to start before stopping Plymouth */
    gboolean stopping_plymouth;

    /* File to log to */
    gchar *log_file;
  
    /* Filename of Mir socket */
    gchar *mir_socket_filename;

    /* Mir compositor process */
    Process *mir_process;

    /* Connection to the compositor */
    MirConnection *mir_connection;

    /* Number of X servers created */
    guint x_server_count;

    /* IDs for each display */
    GHashTable *display_ids;
};

G_DEFINE_TYPE (SeatMir, seat_mir, SEAT_TYPE);

static void
seat_mir_setup (Seat *seat)
{
    seat_set_can_switch (seat, TRUE);
    SEAT_CLASS (seat_mir_parent_class)->setup (seat);
}

static void
compositor_stopped_cb (Process *process, SeatMir *seat)
{
    g_debug ("Stopping Mir seat, compositor terminated");
  
    if (seat->priv->stopping_plymouth)
    {
        g_debug ("Stopping Plymouth, compositor failed to start");
        plymouth_quit (FALSE);
        seat->priv->stopping_plymouth = FALSE;
    }

    seat_stop (SEAT (seat));
}

static void
compositor_run_cb (Process *process, SeatMir *seat)
{
    int fd;

    /* Make input non-blocking */
    fd = open ("/dev/null", O_RDONLY);
    dup2 (fd, STDIN_FILENO);
    close (fd);

    /* Redirect output to logfile */
    if (seat->priv->log_file)
    {
         int fd;

         fd = g_open (seat->priv->log_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
         if (fd < 0)
             g_warning ("Failed to open log file %s: %s", seat->priv->log_file, g_strerror (errno));
         else
         {
             dup2 (fd, STDOUT_FILENO);
             dup2 (fd, STDERR_FILENO);
             close (fd);
         }
    }
}

static void
connected_cb (MirConnection *connection, void *client_context)
{
    SeatMir *seat = client_context;
    seat->priv->mir_connection = connection;
}

static gboolean
seat_mir_start (Seat *seat)
{
    gchar *command, *dir;
    gboolean result;
    guint wait_count;
    MirWaitHandle *handle;

    /* Replace Plymouth if it is running */
    if (plymouth_get_is_active () && plymouth_has_active_vt ())
    {
        gint active_vt = vt_get_active ();
        if (active_vt >= vt_get_min ())
        {
            g_debug ("Compositor will replace Plymouth");
            SEAT_MIR (seat)->priv->vt = active_vt;
            plymouth_deactivate ();
        }
        else
            g_debug ("Plymouth is running on VT %d, but this is less than the configured minimum of %d so not replacing it", active_vt, vt_get_min ());       
    }
    if (SEAT_MIR (seat)->priv->vt < 0)
        SEAT_MIR (seat)->priv->vt = vt_get_unused ();
    if (SEAT_MIR (seat)->priv->vt < 0)
    {
        g_debug ("Failed to get a VT to run on");
        return FALSE;
    }
    vt_ref (SEAT_MIR (seat)->priv->vt);

    /* Setup logging */
    dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    SEAT_MIR (seat)->priv->log_file = g_build_filename (dir, "mir.log", NULL);
    g_debug ("Logging to %s", SEAT_MIR (seat)->priv->log_file);
    g_free (dir);

    /* Start the compositor */
    SEAT_MIR (seat)->priv->mir_socket_filename = g_strdup ("/tmp/mir_socket"); // FIXME: Use this socket by default as XMir is hardcoded to this
    command = g_strdup_printf ("mir --file %s", SEAT_MIR (seat)->priv->mir_socket_filename);
    process_set_command (SEAT_MIR (seat)->priv->mir_process, command);
    g_free (command);
    g_signal_connect (SEAT_MIR (seat)->priv->mir_process, "stopped", G_CALLBACK (compositor_stopped_cb), seat);
    g_signal_connect (SEAT_MIR (seat)->priv->mir_process, "run", G_CALLBACK (compositor_run_cb), seat);
    result = process_start (SEAT_MIR (seat)->priv->mir_process);

    if (!result)
        return FALSE;

    /* Wait for up to a second for the socket to appear */
    for (wait_count = 0; wait_count < 10 && !g_file_test (SEAT_MIR (seat)->priv->mir_socket_filename, G_FILE_TEST_EXISTS); wait_count++)
    {
        g_debug ("Waiting for Mir socket %s...", SEAT_MIR (seat)->priv->mir_socket_filename);      
        g_usleep (100000);
    }

    /* Connect to the compositor */
    g_debug ("Connecting to Mir");
    handle = mir_connect (SEAT_MIR (seat)->priv->mir_socket_filename, "LightDM", connected_cb, seat);
    mir_wait_for (handle);
    if (SEAT_MIR (seat)->priv->mir_connection == NULL)
    {
        g_warning ("No connection from Mir");
        return FALSE;
    }
    else if (!mir_connection_is_valid (SEAT_MIR (seat)->priv->mir_connection))
    {
        g_warning ("Failed to connect to Mir: %s", mir_connection_get_error_message (SEAT_MIR (seat)->priv->mir_connection));
        return FALSE;
    }
    g_debug ("Connected to Mir");

    return SEAT_CLASS (seat_mir_parent_class)->start (SEAT (seat));
}

static DisplayServer *
seat_mir_create_display_server (Seat *seat)
{
    XServerLocal *xserver;
    const gchar *command = NULL, *layout = NULL, *config_file = NULL, *xdmcp_manager = NULL, *key_name = NULL;
    gboolean allow_tcp;
    gint port = 0;
    gint id;

    g_debug ("Starting X server on Mir compositor");

    xserver = xserver_local_new ();

    id = SEAT_MIR (seat)->priv->x_server_count;
    SEAT_MIR (seat)->priv->x_server_count++;
    xserver_local_set_mir_id (xserver, id);
    xserver_local_set_mir_socket (xserver, SEAT_MIR (seat)->priv->mir_socket_filename);
    g_hash_table_insert (SEAT_MIR (seat)->priv->display_ids, g_object_ref (xserver), GINT_TO_POINTER (id));

    command = seat_get_string_property (seat, "xserver-command");
    if (command)
        xserver_local_set_command (xserver, command);

    layout = seat_get_string_property (seat, "xserver-layout");
    if (layout)
        xserver_local_set_layout (xserver, layout);

    config_file = seat_get_string_property (seat, "xserver-config");
    if (config_file)
        xserver_local_set_config (xserver, config_file);
  
    allow_tcp = seat_get_boolean_property (seat, "xserver-allow-tcp");
    xserver_local_set_allow_tcp (xserver, allow_tcp);    

    xdmcp_manager = seat_get_string_property (seat, "xdmcp-manager");
    if (xdmcp_manager)
        xserver_local_set_xdmcp_server (xserver, xdmcp_manager);

    port = seat_get_integer_property (seat, "xdmcp-port");
    if (port > 0)
        xserver_local_set_xdmcp_port (xserver, port);

    key_name = seat_get_string_property (seat, "xdmcp-key");
    if (key_name)
    {
        gchar *dir, *path;
        GKeyFile *keys;
        gboolean result;
        GError *error = NULL;

        dir = config_get_string (config_get_instance (), "LightDM", "config-directory");
        path = g_build_filename (dir, "keys.conf", NULL);
        g_free (dir);

        keys = g_key_file_new ();
        result = g_key_file_load_from_file (keys, path, G_KEY_FILE_NONE, &error);
        if (error)
            g_debug ("Error getting key %s", error->message);
        g_clear_error (&error);      

        if (result)
        {
            gchar *key = NULL;

            if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                key = g_key_file_get_string (keys, "keyring", key_name, NULL);
            else
                g_debug ("Key %s not defined", key_name);

            if (key)
                xserver_local_set_xdmcp_key (xserver, key);
            g_free (key);
        }

        g_free (path);
        g_key_file_free (keys);
    }

    return DISPLAY_SERVER (xserver);
}

static Session *
seat_mir_create_session (Seat *seat, Display *display)
{
    XServerLocal *xserver;
    XSession *session;
    gchar *tty;

    xserver = XSERVER_LOCAL (display_get_display_server (display));

    session = xsession_new (XSERVER (xserver));
    tty = g_strdup_printf ("/dev/tty%d", SEAT_MIR (seat)->priv->vt);
    session_set_tty (SESSION (session), tty);
    g_free (tty);

    return SESSION (session);
}

static void
seat_mir_set_active_display (Seat *seat, Display *display)
{
    gint id;

    id = GPOINTER_TO_INT (g_hash_table_lookup (SEAT_MIR (seat)->priv->display_ids, display_get_display_server (display)));
    mir_select_focus_by_lightdm_id (SEAT_MIR (seat)->priv->mir_connection, id);

    SEAT_CLASS (seat_mir_parent_class)->set_active_display (seat, display);
}

static void
seat_mir_run_script (Seat *seat, Display *display, Process *script)
{
    gchar *path;
    XServerLocal *xserver;

    xserver = XSERVER_LOCAL (display_get_display_server (display));
    path = xserver_local_get_authority_file_path (xserver);
    process_set_env (script, "DISPLAY", xserver_get_address (XSERVER (xserver)));
    process_set_env (script, "XAUTHORITY", path);
    g_free (path);

    SEAT_CLASS (seat_mir_parent_class)->run_script (seat, display, script);
}

static void
seat_mir_stop (Seat *seat)
{
    process_stop (SEAT_MIR (seat)->priv->mir_process);

    SEAT_CLASS (seat_mir_parent_class)->stop (seat);
}

static void
seat_mir_display_removed (Seat *seat, Display *display)
{
    g_hash_table_remove (SEAT_MIR (seat)->priv->display_ids, display_get_display_server (display));

    if (seat_get_is_stopping (seat))
        return;

    /* If this is the only display and it failed to start then stop this seat */
    if (g_list_length (seat_get_displays (seat)) == 0 && !display_get_is_ready (display))
    {
        g_debug ("Stopping Mir seat, failed to start a display");
        seat_stop (seat);
        return;
    }

    /* Show a new greeter */  
    if (display == seat_get_active_display (seat))
    {
        g_debug ("Active display stopped, switching to greeter");
        seat_switch_to_greeter (seat);
    }
}

static void
seat_mir_init (SeatMir *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_MIR_TYPE, SeatMirPrivate);
    seat->priv->vt = -1;
    seat->priv->mir_process = process_new ();
    seat->priv->x_server_count = 0;
    seat->priv->display_ids = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, NULL);
}

static void
seat_mir_finalize (GObject *object)
{
    SeatMir *seat = SEAT_MIR (object);

    if (seat->priv->vt >= 0)
       vt_unref (seat->priv->vt);
    g_free (seat->priv->log_file);
    g_free (seat->priv->mir_socket_filename);
    g_object_unref (seat->priv->mir_process);

    G_OBJECT_CLASS (seat_mir_parent_class)->finalize (object);
}

static void
seat_mir_class_init (SeatMirClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);  
    SeatClass *seat_class = SEAT_CLASS (klass);

    object_class->finalize = seat_mir_finalize;
    seat_class->setup = seat_mir_setup;
    seat_class->start = seat_mir_start;
    seat_class->create_display_server = seat_mir_create_display_server;
    seat_class->create_session = seat_mir_create_session;
    seat_class->set_active_display = seat_mir_set_active_display;
    seat_class->run_script = seat_mir_run_script;
    seat_class->stop = seat_mir_stop;  
    seat_class->display_removed = seat_mir_display_removed;

    g_type_class_add_private (klass, sizeof (SeatMirPrivate));
}
