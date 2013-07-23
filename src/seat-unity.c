/*
 * Copyright (C) 2012-2013 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <glib/gstdio.h>

#include "seat-unity.h"
#include "configuration.h"
#include "xserver-local.h"
#include "xgreeter.h"
#include "xsession.h"
#include "mir-server.h"
#include "vt.h"
#include "plymouth.h"

typedef enum
{
   USC_MESSAGE_PING = 0,
   USC_MESSAGE_PONG = 1,
   USC_MESSAGE_READY = 2,
   USC_MESSAGE_SESSION_CONNECTED = 3,
   USC_MESSAGE_SET_ACTIVE_SESSION = 4
} USCMessageID;

struct SeatUnityPrivate
{
    /* VT we are running on */
    gint vt;

    /* TRUE if waiting for X server to start before stopping Plymouth */
    gboolean stopping_plymouth;

    /* File to log to */
    gchar *log_file;

    /* Filename of Mir socket */
    gchar *mir_socket_filename;

    /* Pipes to communicate with compositor */
    int to_compositor_pipe[2];
    int from_compositor_pipe[2];

    /* IO channel listening on for messages from the compositor */
    GIOChannel *from_compositor_channel;

    /* TRUE when the compositor indicates it is ready */
    gboolean compositor_ready;

    /* Buffer reading from channel */
    guint8 *read_buffer;
    gsize read_buffer_length;
    gsize read_buffer_n_used;

    /* Compositor process */
    Process *compositor_process;

    /* Timeout when waiting for compositor to start */
    guint compositor_timeout;

    /* Next Mir ID to use for a compositor client */
    gint next_id;

    /* TRUE if using VT switching fallback */
    gboolean use_vt_switching;

    /* The currently visible session */
    Session *active_session;
    DisplayServer *active_display_server;
};

G_DEFINE_TYPE (SeatUnity, seat_unity, SEAT_TYPE);

static void
seat_unity_setup (Seat *seat)
{
    seat_set_can_switch (seat, TRUE);
    SEAT_CLASS (seat_unity_parent_class)->setup (seat);
}

static void
compositor_stopped_cb (Process *process, SeatUnity *seat)
{
    if (seat->priv->compositor_timeout != 0)
        g_source_remove (seat->priv->compositor_timeout);
    seat->priv->compositor_timeout = 0;

    /* Finished with the VT */
    vt_unref (seat->priv->vt);
    seat->priv->vt = -1;

    if (seat_get_is_stopping (SEAT (seat)))
    {
        SEAT_CLASS (seat_unity_parent_class)->stop (SEAT (seat));
        return;
    }

    /* If stopped before it was ready, then revert to VT mode */
    if (!seat->priv->compositor_ready)
    {
        g_debug ("Compositor failed to start, switching to VT mode");
        seat->priv->use_vt_switching = TRUE;
        SEAT_CLASS (seat_unity_parent_class)->start (SEAT (seat));
        return;
    }

    g_debug ("Stopping Unity seat, compositor terminated");

    if (seat->priv->stopping_plymouth)
    {
        g_debug ("Stopping Plymouth, compositor failed to start");
        plymouth_quit (FALSE);
        seat->priv->stopping_plymouth = FALSE;
    }

    seat_stop (SEAT (seat));
}

static void
compositor_run_cb (Process *process, SeatUnity *seat)
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

    if (seat->priv->stopping_plymouth)
    {      
        seat->priv->stopping_plymouth = FALSE;
        plymouth_quit (TRUE);
    }  
}

static void
write_message (SeatUnity *seat, guint16 id, const guint8 *payload, guint16 payload_length)
{
    guint8 *data;
    gsize data_length = 4 + payload_length;

    data = g_malloc (data_length);
    data[0] = id >> 8;
    data[1] = id & 0xFF;
    data[2] = payload_length >> 8;
    data[3] = payload_length & 0xFF;
    memcpy (data + 4, payload, payload_length);

    errno = 0;
    if (write (seat->priv->to_compositor_pipe[1], data, data_length) != data_length)
        g_warning ("Failed to write to compositor: %s", strerror (errno));
}

static gboolean
read_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    SeatUnity *seat = data;
    gsize n_to_read = 0;
    guint16 id, payload_length;
    /*guint8 *payload;*/
  
    if (condition == G_IO_HUP)
    {
        g_debug ("Compositor closed communication channel");
        return FALSE;
    }

    /* Work out how much required for a message */
    if (seat->priv->read_buffer_n_used < 4)
        n_to_read = 4 - seat->priv->read_buffer_n_used;
    else
    {
        payload_length = seat->priv->read_buffer[2] << 8 | seat->priv->read_buffer[3];
        n_to_read = 4 + payload_length - seat->priv->read_buffer_n_used;
    }

    /* Read from compositor */
    if (n_to_read > 0)
    {
        gsize n_total, n_read = 0;
        GIOStatus status;
        GError *error = NULL;

        n_total = seat->priv->read_buffer_n_used + n_to_read;
        if (seat->priv->read_buffer_length < n_total)
            seat->priv->read_buffer = g_realloc (seat->priv->read_buffer, n_total);

        status = g_io_channel_read_chars (source,
                                          (gchar *)seat->priv->read_buffer + seat->priv->read_buffer_n_used,
                                          n_to_read,
                                          &n_read,
                                          &error);
        if (error)
            g_warning ("Failed to read from compositor: %s", error->message);
        if (status != G_IO_STATUS_NORMAL)
            return TRUE;
        g_clear_error (&error);
        seat->priv->read_buffer_n_used += n_read;
    }

    /* Read header */
    if (seat->priv->read_buffer_n_used < 4)
         return TRUE;
    id = seat->priv->read_buffer[0] << 8 | seat->priv->read_buffer[1];
    payload_length = seat->priv->read_buffer[2] << 8 | seat->priv->read_buffer[3];

    /* Read payload */
    if (seat->priv->read_buffer_n_used < 4 + payload_length)
        return TRUE;
    /*payload = seat->priv->read_buffer + 4;*/

    switch (id)
    {
    case USC_MESSAGE_PING:
        g_debug ("PING!");
        write_message (seat, USC_MESSAGE_PONG, NULL, 0);
        break;
    case USC_MESSAGE_PONG:
        g_debug ("PONG!");
        break;
    case USC_MESSAGE_READY:
        g_debug ("READY");
        if (!seat->priv->compositor_ready)
        {
            seat->priv->compositor_ready = TRUE;
            g_debug ("Compositor ready");
            g_source_remove (seat->priv->compositor_timeout);
            seat->priv->compositor_timeout = 0;
            SEAT_CLASS (seat_unity_parent_class)->start (SEAT (seat));
        }
        break;
    case USC_MESSAGE_SESSION_CONNECTED:
        g_debug ("SESSION CONNECTED");
        break;
    default:
        g_warning ("Ingoring unknown message %d with %d octets from system compositor", id, payload_length);
        break;
    }

    /* Clear buffer */
    seat->priv->read_buffer_n_used = 0;

    return TRUE;
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
        g_free (absolute_binary);
    }
    else
        absolute_command = g_strdup (command);

    g_strfreev (tokens);

    return absolute_command;
}

static gboolean
compositor_timeout_cb (gpointer data)
{
    SeatUnity *seat = data;

    /* Stop the compositor - it is not working */
    process_stop (seat->priv->compositor_process);

    return TRUE;
}

static gboolean
seat_unity_start (Seat *seat)
{
    const gchar *compositor_command;
    gchar *command, *absolute_command, *dir;
    gboolean result;
    int timeout;

    /* Replace Plymouth if it is running */
    if (plymouth_get_is_active () && plymouth_has_active_vt ())
    {
        gint active_vt = vt_get_active ();
        if (active_vt >= vt_get_min ())
        {
            g_debug ("Compositor will replace Plymouth");
            SEAT_UNITY (seat)->priv->stopping_plymouth = TRUE;
            SEAT_UNITY (seat)->priv->vt = active_vt;
            plymouth_deactivate ();
        }
        else
            g_debug ("Plymouth is running on VT %d, but this is less than the configured minimum of %d so not replacing it", active_vt, vt_get_min ());
    }
    if (SEAT_UNITY (seat)->priv->vt < 0)
        SEAT_UNITY (seat)->priv->vt = vt_get_unused ();
    if (SEAT_UNITY (seat)->priv->vt < 0)
    {
        g_debug ("Failed to get a VT to run on");
        return FALSE;
    }
    vt_ref (SEAT_UNITY (seat)->priv->vt);

    /* Create pipes to talk to compositor */
    if (pipe (SEAT_UNITY (seat)->priv->to_compositor_pipe) < 0 || pipe (SEAT_UNITY (seat)->priv->from_compositor_pipe) < 0)
    {
        g_debug ("Failed to create compositor pipes: %s", g_strerror (errno));
        return FALSE;
    }

    /* Don't allow the daemon end of the pipes to be accessed in the compositor */
    fcntl (SEAT_UNITY (seat)->priv->to_compositor_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl (SEAT_UNITY (seat)->priv->from_compositor_pipe[0], F_SETFD, FD_CLOEXEC);

    /* Listen for messages from the compositor */
    SEAT_UNITY (seat)->priv->from_compositor_channel = g_io_channel_unix_new (SEAT_UNITY (seat)->priv->from_compositor_pipe[0]);
    g_io_add_watch (SEAT_UNITY (seat)->priv->from_compositor_channel, G_IO_IN | G_IO_HUP, read_cb, seat);

    /* Setup logging */
    dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    SEAT_UNITY (seat)->priv->log_file = g_build_filename (dir, "unity-system-compositor.log", NULL);
    g_debug ("Logging to %s", SEAT_UNITY (seat)->priv->log_file);
    g_free (dir);

    SEAT_UNITY (seat)->priv->mir_socket_filename = g_strdup ("/tmp/mir_socket"); // FIXME: Use this socket by default as XMir is hardcoded to this
    timeout = seat_get_integer_property (seat, "unity-compositor-timeout");
    compositor_command = seat_get_string_property (seat, "unity-compositor-command");
    command = g_strdup_printf ("%s --from-dm-fd %d --to-dm-fd %d --vt %d", compositor_command, SEAT_UNITY (seat)->priv->to_compositor_pipe[0], SEAT_UNITY (seat)->priv->from_compositor_pipe[1], SEAT_UNITY (seat)->priv->vt);

    absolute_command = get_absolute_command (command);
    g_free (command);

    /* Start the compositor */
    process_set_command (SEAT_UNITY (seat)->priv->compositor_process, absolute_command);
    g_free (absolute_command);
    g_signal_connect (SEAT_UNITY (seat)->priv->compositor_process, "stopped", G_CALLBACK (compositor_stopped_cb), seat);
    g_signal_connect (SEAT_UNITY (seat)->priv->compositor_process, "run", G_CALLBACK (compositor_run_cb), seat);
    result = process_start (SEAT_UNITY (seat)->priv->compositor_process, FALSE);

    /* Close compostor ends of the pipes */
    close (SEAT_UNITY (seat)->priv->to_compositor_pipe[0]);
    SEAT_UNITY (seat)->priv->to_compositor_pipe[0] = 0;
    close (SEAT_UNITY (seat)->priv->from_compositor_pipe[1]);
    SEAT_UNITY (seat)->priv->from_compositor_pipe[1] = 0;

    if (!result)
        return FALSE;

    /* Connect to the compositor */
    timeout = seat_get_integer_property (seat, "unity-compositor-timeout");
    if (timeout <= 0)
        timeout = 60;
    g_debug ("Waiting for system compositor for %ds", timeout);
    SEAT_UNITY (seat)->priv->compositor_timeout = g_timeout_add (timeout * 1000, compositor_timeout_cb, seat);

    return TRUE;
}

static DisplayServer *
create_x_server (Seat *seat)
{
    XServerLocal *xserver;
    const gchar *command = NULL, *layout = NULL, *config_file = NULL, *xdmcp_manager = NULL, *key_name = NULL;
    gboolean allow_tcp;
    gint port = 0;
    gchar *id;

    g_debug ("Starting X server on Unity compositor");

    xserver = xserver_local_new ();

    if (!SEAT_UNITY (seat)->priv->use_vt_switching)
    {
        id = g_strdup_printf ("%d", SEAT_UNITY (seat)->priv->next_id);
        SEAT_UNITY (seat)->priv->next_id++;
        xserver_local_set_mir_id (xserver, id);
        xserver_local_set_mir_socket (xserver, SEAT_UNITY (seat)->priv->mir_socket_filename);
        g_free (id);
    }

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

static DisplayServer *
seat_unity_create_display_server (Seat *seat, const gchar *session_type)
{  
    if (strcmp (session_type, "x") == 0)
        return create_x_server (seat);
    else if (strcmp (session_type, "mir") == 0)
        return DISPLAY_SERVER (mir_server_new ());
    else
    {
        g_warning ("Can't create unsupported display server '%s'", session_type);
        return NULL;
    }
}

static Greeter *
seat_unity_create_greeter_session (Seat *seat)
{
    XGreeter *greeter_session;

    greeter_session = xgreeter_new ();
    session_set_env (SESSION (greeter_session), "XDG_SEAT", "seat0");

    return GREETER (greeter_session);
}

static Session *
seat_unity_create_session (Seat *seat)
{
    XSession *session;

    session = xsession_new ();
    session_set_env (SESSION (session), "XDG_SEAT", "seat0");

    return SESSION (session);
}

static void
seat_unity_set_active_session (Seat *seat, Session *session)
{
    DisplayServer *display_server;

    /* If no compositor, have to use VT switching */
    if (SEAT_UNITY (seat)->priv->use_vt_switching)
    {
        gint vt = display_server_get_vt (session_get_display_server (session));
        if (vt >= 0)
            vt_set_active (vt);

        SEAT_CLASS (seat_unity_parent_class)->set_active_session (seat, session);
        return;
    }

    if (session == SEAT_UNITY (seat)->priv->active_session)
        return;
    SEAT_UNITY (seat)->priv->active_session = g_object_ref (session);

    display_server = session_get_display_server (session);
    if (SEAT_UNITY (seat)->priv->active_display_server != display_server)
    {
        const gchar *id;

        SEAT_UNITY (seat)->priv->active_display_server = g_object_ref (display_server);
        id = xserver_local_get_mir_id (XSERVER_LOCAL (display_server));

        g_debug ("Switching to Mir session %s", id);
        write_message (SEAT_UNITY (seat), USC_MESSAGE_SET_ACTIVE_SESSION, (const guint8 *) id, strlen (id));
    }

    SEAT_CLASS (seat_unity_parent_class)->set_active_session (seat, session);
}

static Session *
seat_unity_get_active_session (Seat *seat)
{
    if (SEAT_UNITY (seat)->priv->use_vt_switching)
    {
        gint vt;
        GList *link;
        vt = vt_get_active ();
        if (vt < 0)
            return NULL;

        for (link = seat_get_sessions (seat); link; link = link->next)
        {
            Session *session = link->data;
            if (display_server_get_vt (session_get_display_server (session)) == vt)
                return session;
        }

        return NULL;
    }

    return SEAT_UNITY (seat)->priv->active_session;
}

static void
seat_unity_run_script (Seat *seat, DisplayServer *display_server, Process *script)
{
    const gchar *path;
    XServerLocal *xserver;

    xserver = XSERVER_LOCAL (display_server);
    path = xserver_local_get_authority_file_path (xserver);
    process_set_env (script, "DISPLAY", xserver_get_address (XSERVER (xserver)));
    process_set_env (script, "XAUTHORITY", path);

    SEAT_CLASS (seat_unity_parent_class)->run_script (seat, display_server, script);
}

static void
seat_unity_stop (Seat *seat)
{
    /* Stop the compositor first */
    if (process_get_is_running (SEAT_UNITY (seat)->priv->compositor_process))
    {
        process_stop (SEAT_UNITY (seat)->priv->compositor_process);
        return;
    }

    SEAT_CLASS (seat_unity_parent_class)->stop (seat);
}

static void
seat_unity_init (SeatUnity *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_UNITY_TYPE, SeatUnityPrivate);
    seat->priv->vt = -1;
    seat->priv->compositor_process = process_new ();
}

static void
seat_unity_finalize (GObject *object)
{
    SeatUnity *seat = SEAT_UNITY (object);

    if (seat->priv->vt >= 0)
        vt_unref (seat->priv->vt);
    g_free (seat->priv->log_file);
    g_free (seat->priv->mir_socket_filename);
    close (seat->priv->to_compositor_pipe[0]);
    close (seat->priv->to_compositor_pipe[1]);
    close (seat->priv->from_compositor_pipe[0]);
    close (seat->priv->from_compositor_pipe[1]);
    g_io_channel_unref (seat->priv->from_compositor_channel);
    g_free (seat->priv->read_buffer);
    g_object_unref (seat->priv->compositor_process);
    if (seat->priv->active_session)
        g_object_unref (seat->priv->active_session);
    if (seat->priv->active_display_server)
        g_object_unref (seat->priv->active_display_server);

    G_OBJECT_CLASS (seat_unity_parent_class)->finalize (object);
}

static void
seat_unity_class_init (SeatUnityClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SeatClass *seat_class = SEAT_CLASS (klass);

    object_class->finalize = seat_unity_finalize;
    seat_class->setup = seat_unity_setup;
    seat_class->start = seat_unity_start;
    seat_class->create_display_server = seat_unity_create_display_server;
    seat_class->create_greeter_session = seat_unity_create_greeter_session;
    seat_class->create_session = seat_unity_create_session;
    seat_class->set_active_session = seat_unity_set_active_session;
    seat_class->get_active_session = seat_unity_get_active_session;
    seat_class->run_script = seat_unity_run_script;
    seat_class->stop = seat_unity_stop;

    g_type_class_add_private (klass, sizeof (SeatUnityPrivate));
}
