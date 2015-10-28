/*
 * Copyright (C) 2013 Canonical Ltd.
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

#include "unity-system-compositor.h"
#include "configuration.h"
#include "process.h"
#include "greeter.h"
#include "vt.h"

struct UnitySystemCompositorPrivate
{
    /* Compositor process */
    Process *process;

    /* Command to run the compositor */
    gchar *command;

    /* Socket to communicate on */
    gchar *socket;

    /* VT to run on */
    gint vt;
    gboolean have_vt_ref;

    /* TRUE if should show hardware cursor */
    gboolean enable_hardware_cursor;

    /* Pipes to communicate with compositor */
    int to_compositor_pipe[2];
    int from_compositor_pipe[2];

    /* IO channel listening on for messages from the compositor */
    GIOChannel *from_compositor_channel;
    guint from_compositor_watch;

    /* Buffer reading from channel */
    guint8 *read_buffer;
    gsize read_buffer_length;
    gsize read_buffer_n_used;

    /* Timeout when waiting for compositor to start */
    gint timeout;
    guint timeout_source;

    /* TRUE when received ready signal */
    gboolean is_ready;
};

G_DEFINE_TYPE (UnitySystemCompositor, unity_system_compositor, DISPLAY_SERVER_TYPE);

typedef enum
{
   USC_MESSAGE_PING = 0,
   USC_MESSAGE_PONG = 1,
   USC_MESSAGE_READY = 2,
   USC_MESSAGE_SESSION_CONNECTED = 3,
   USC_MESSAGE_SET_ACTIVE_SESSION = 4,
   USC_MESSAGE_SET_NEXT_SESSION = 5,
} USCMessageID;

UnitySystemCompositor *
unity_system_compositor_new (void)
{
    return g_object_new (UNITY_SYSTEM_COMPOSITOR_TYPE, NULL);
}

void
unity_system_compositor_set_command (UnitySystemCompositor *compositor, const gchar *command)
{
    g_return_if_fail (compositor != NULL);
    g_free (compositor->priv->command);
    compositor->priv->command = g_strdup (command);
}

void
unity_system_compositor_set_socket (UnitySystemCompositor *compositor, const gchar *socket)
{
    g_return_if_fail (compositor != NULL);
    g_free (compositor->priv->socket);
    compositor->priv->socket = g_strdup (socket);
}

const gchar *
unity_system_compositor_get_socket (UnitySystemCompositor *compositor)
{
    g_return_val_if_fail (compositor != NULL, NULL);
    return compositor->priv->socket;
}

void
unity_system_compositor_set_vt (UnitySystemCompositor *compositor, gint vt)
{
    g_return_if_fail (compositor != NULL);

    if (compositor->priv->have_vt_ref)
        vt_unref (compositor->priv->vt);
    compositor->priv->have_vt_ref = FALSE;
    compositor->priv->vt = vt;
    if (vt > 0)
    {
        vt_ref (vt);
        compositor->priv->have_vt_ref = TRUE;
    }
}

void
unity_system_compositor_set_enable_hardware_cursor (UnitySystemCompositor *compositor, gboolean enable_cursor)
{
    g_return_if_fail (compositor != NULL);
    compositor->priv->enable_hardware_cursor = enable_cursor;
}

void
unity_system_compositor_set_timeout (UnitySystemCompositor *compositor, gint timeout)
{
    g_return_if_fail (compositor != NULL);
    compositor->priv->timeout = timeout;
}

static void
write_message (UnitySystemCompositor *compositor, guint16 id, const guint8 *payload, guint16 payload_length)
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
    if (write (compositor->priv->to_compositor_pipe[1], data, data_length) != data_length)
        l_warning (compositor, "Failed to write to compositor: %s", strerror (errno));

    g_free (data);
}

void
unity_system_compositor_set_active_session (UnitySystemCompositor *compositor, const gchar *id)
{
    g_return_if_fail (compositor != NULL);
    write_message (compositor, USC_MESSAGE_SET_ACTIVE_SESSION, (const guint8 *) id, strlen (id));
}

void
unity_system_compositor_set_next_session (UnitySystemCompositor *compositor, const gchar *id)
{
    g_return_if_fail (compositor != NULL);
    write_message (compositor, USC_MESSAGE_SET_NEXT_SESSION, (const guint8 *) id, strlen (id));
}

static gint
unity_system_compositor_get_vt (DisplayServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return UNITY_SYSTEM_COMPOSITOR (server)->priv->vt;
}

static void
unity_system_compositor_connect_session (DisplayServer *display_server, Session *session)
{
    UnitySystemCompositor *compositor = UNITY_SYSTEM_COMPOSITOR (display_server);
    const gchar *name;

    if (compositor->priv->socket)
        session_set_env (session, "MIR_SOCKET", compositor->priv->socket);
    if (IS_GREETER (session))
        name = "greeter-0";
    else
        name = "session-0";
    session_set_env (session, "MIR_SERVER_NAME", name);

    if (compositor->priv->vt >= 0)
    {
        gchar *value = g_strdup_printf ("%d", compositor->priv->vt);
        session_set_env (session, "XDG_VTNR", value);
        g_free (value);
    }
}

static void
unity_system_compositor_disconnect_session (DisplayServer *display_server, Session *session)
{
    session_unset_env (session, "MIR_SOCKET");
    session_unset_env (session, "MIR_SERVER_NAME");
    session_unset_env (session, "XDG_VTNR");
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

static gboolean
read_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    UnitySystemCompositor *compositor = data;
    gsize n_to_read = 0;
    guint16 id, payload_length;
    /*guint8 *payload;*/
  
    if (condition == G_IO_HUP)
    {
        l_debug (compositor, "Compositor closed communication channel");
        compositor->priv->from_compositor_watch = 0;
        return FALSE;
    }

    /* Work out how much required for a message */
    if (compositor->priv->read_buffer_n_used < 4)
        n_to_read = 4 - compositor->priv->read_buffer_n_used;
    else
    {
        payload_length = compositor->priv->read_buffer[2] << 8 | compositor->priv->read_buffer[3];
        n_to_read = 4 + payload_length - compositor->priv->read_buffer_n_used;
    }

    /* Read from compositor */
    if (n_to_read > 0)
    {
        gsize n_total, n_read = 0;
        GIOStatus status;
        GError *error = NULL;

        n_total = compositor->priv->read_buffer_n_used + n_to_read;
        if (compositor->priv->read_buffer_length < n_total)
            compositor->priv->read_buffer = g_realloc (compositor->priv->read_buffer, n_total);

        status = g_io_channel_read_chars (source,
                                          (gchar *)compositor->priv->read_buffer + compositor->priv->read_buffer_n_used,
                                          n_to_read,
                                          &n_read,
                                          &error);
        if (error)
            l_warning (compositor, "Failed to read from compositor: %s", error->message);
        if (status != G_IO_STATUS_NORMAL)
            return TRUE;
        g_clear_error (&error);
        compositor->priv->read_buffer_n_used += n_read;
    }

    /* Read header */
    if (compositor->priv->read_buffer_n_used < 4)
         return TRUE;
    id = compositor->priv->read_buffer[0] << 8 | compositor->priv->read_buffer[1];
    payload_length = compositor->priv->read_buffer[2] << 8 | compositor->priv->read_buffer[3];

    /* Read payload */
    if (compositor->priv->read_buffer_n_used < 4 + payload_length)
        return TRUE;
    /*payload = compositor->priv->read_buffer + 4;*/

    switch (id)
    {
    case USC_MESSAGE_PING:
        l_debug (compositor, "PING!");
        write_message (compositor, USC_MESSAGE_PONG, NULL, 0);
        break;
    case USC_MESSAGE_PONG:
        l_debug (compositor, "PONG!");
        break;
    case USC_MESSAGE_READY:
        l_debug (compositor, "READY");
        if (!compositor->priv->is_ready)
        {
            compositor->priv->is_ready = TRUE;
            l_debug (compositor, "Compositor ready");
            g_source_remove (compositor->priv->timeout_source);
            compositor->priv->timeout_source = 0;
            DISPLAY_SERVER_CLASS (unity_system_compositor_parent_class)->start (DISPLAY_SERVER (compositor));
        }
        break;
    case USC_MESSAGE_SESSION_CONNECTED:
        l_debug (compositor, "SESSION CONNECTED");
        break;
    default:
        l_warning (compositor, "Ignoring unknown message %d with %d octets from system compositor", id, payload_length);
        break;
    }

    /* Clear buffer */
    compositor->priv->read_buffer_n_used = 0;

    return TRUE;
}

static void
run_cb (Process *process, gpointer user_data)
{
    int fd;

    /* Make input non-blocking */
    fd = open ("/dev/null", O_RDONLY);
    dup2 (fd, STDIN_FILENO);
    close (fd);
}

static gboolean
timeout_cb (gpointer data)
{
    UnitySystemCompositor *compositor = data;

    /* Stop the compositor - it is not working */
    display_server_stop (DISPLAY_SERVER (compositor));

    compositor->priv->timeout_source = 0;

    return TRUE;
}

static void
stopped_cb (Process *process, UnitySystemCompositor *compositor)
{
    l_debug (compositor, "Unity system compositor stopped");

    if (compositor->priv->timeout_source != 0)
        g_source_remove (compositor->priv->timeout_source);
    compositor->priv->timeout_source = 0;

    /* Release VT and display number for re-use */
    if (compositor->priv->have_vt_ref)
    {
        vt_unref (compositor->priv->vt);
        compositor->priv->have_vt_ref = FALSE;
    }

    DISPLAY_SERVER_CLASS (unity_system_compositor_parent_class)->stop (DISPLAY_SERVER (compositor));
}

static gboolean
unity_system_compositor_start (DisplayServer *server)
{
    UnitySystemCompositor *compositor = UNITY_SYSTEM_COMPOSITOR (server);
    gboolean result, backup_logs;
    GString *command;
    gchar *dir, *log_file, *absolute_command, *value;

    g_return_val_if_fail (compositor->priv->process == NULL, FALSE);

    compositor->priv->is_ready = FALSE;

    g_return_val_if_fail (compositor->priv->command != NULL, FALSE);

    /* Create pipes to talk to compositor */
    if (pipe (compositor->priv->to_compositor_pipe) < 0 || pipe (compositor->priv->from_compositor_pipe) < 0)
    {
        l_debug (compositor, "Failed to create compositor pipes: %s", g_strerror (errno));
        return FALSE;
    }

    /* Don't allow the daemon end of the pipes to be accessed in the compositor */
    fcntl (compositor->priv->to_compositor_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl (compositor->priv->from_compositor_pipe[0], F_SETFD, FD_CLOEXEC);

    /* Listen for messages from the compositor */
    compositor->priv->from_compositor_channel = g_io_channel_unix_new (compositor->priv->from_compositor_pipe[0]);
    compositor->priv->from_compositor_watch = g_io_add_watch (compositor->priv->from_compositor_channel, G_IO_IN | G_IO_HUP, read_cb, compositor);

    /* Setup logging */
    dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    log_file = g_build_filename (dir, "unity-system-compositor.log", NULL);
    l_debug (compositor, "Logging to %s", log_file);
    g_free (dir);

    /* Setup environment */
    compositor->priv->process = process_new (run_cb, compositor);
    backup_logs = config_get_boolean (config_get_instance (), "LightDM", "backup-logs");
    process_set_log_file (compositor->priv->process, log_file, TRUE, backup_logs ? LOG_MODE_BACKUP_AND_TRUNCATE : LOG_MODE_APPEND);
    g_free (log_file);
    process_set_clear_environment (compositor->priv->process, TRUE);
    process_set_env (compositor->priv->process, "XDG_SEAT", "seat0");
    value = g_strdup_printf ("%d", compositor->priv->vt);
    process_set_env (compositor->priv->process, "XDG_VTNR", value);
    g_free (value);
    /* Variable required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
    {
        process_set_env (compositor->priv->process, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));
        process_set_env (compositor->priv->process, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
        process_set_env (compositor->priv->process, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    }

    /* Generate command line to run */
    absolute_command = get_absolute_command (compositor->priv->command);
    if (!absolute_command)
    {
        l_debug (compositor, "Can't launch compositor %s, not found in path", compositor->priv->command);
        return FALSE;
    }
    command = g_string_new (absolute_command);
    g_free (absolute_command);
    g_string_append_printf (command, " --file '%s'", compositor->priv->socket);
    g_string_append_printf (command, " --from-dm-fd %d --to-dm-fd %d", compositor->priv->to_compositor_pipe[0], compositor->priv->from_compositor_pipe[1]);
    if (compositor->priv->vt > 0)
        g_string_append_printf (command, " --vt %d", compositor->priv->vt);
    if (compositor->priv->enable_hardware_cursor)
        g_string_append (command, " --enable-hardware-cursor=true");
    process_set_command (compositor->priv->process, command->str);
    g_string_free (command, TRUE);

    /* Start the compositor */
    g_signal_connect (compositor->priv->process, "stopped", G_CALLBACK (stopped_cb), compositor);
    result = process_start (compositor->priv->process, FALSE);

    /* Close compostor ends of the pipes */
    close (compositor->priv->to_compositor_pipe[0]);
    compositor->priv->to_compositor_pipe[0] = 0;
    close (compositor->priv->from_compositor_pipe[1]);
    compositor->priv->from_compositor_pipe[1] = 0;

    if (!result)
        return FALSE;

    /* Connect to the compositor */
    if (compositor->priv->timeout > 0)
    {
        l_debug (compositor, "Waiting for system compositor for %ds", compositor->priv->timeout);
        compositor->priv->timeout_source = g_timeout_add (compositor->priv->timeout * 1000, timeout_cb, compositor);
    }

    return TRUE;
}
 
static void
unity_system_compositor_stop (DisplayServer *server)
{
    process_stop (UNITY_SYSTEM_COMPOSITOR (server)->priv->process);
}

static void
unity_system_compositor_init (UnitySystemCompositor *compositor)
{
    compositor->priv = G_TYPE_INSTANCE_GET_PRIVATE (compositor, UNITY_SYSTEM_COMPOSITOR_TYPE, UnitySystemCompositorPrivate);
    compositor->priv->vt = -1;
    compositor->priv->command = g_strdup ("unity-system-compositor");
    compositor->priv->socket = g_strdup ("/tmp/mir_socket");
    compositor->priv->timeout = -1;
}

static void
unity_system_compositor_finalize (GObject *object)
{
    UnitySystemCompositor *self;

    self = UNITY_SYSTEM_COMPOSITOR (object);  

    if (self->priv->process)
    {
        g_signal_handlers_disconnect_matched (self->priv->process, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
        g_object_unref (self->priv->process);
    }
    g_free (self->priv->command);
    g_free (self->priv->socket);
    if (self->priv->have_vt_ref)
        vt_unref (self->priv->vt);
    close (self->priv->to_compositor_pipe[0]);
    close (self->priv->to_compositor_pipe[1]);
    close (self->priv->from_compositor_pipe[0]);
    close (self->priv->from_compositor_pipe[1]);
    g_io_channel_unref (self->priv->from_compositor_channel);
    if (self->priv->from_compositor_watch)      
        g_source_remove (self->priv->from_compositor_watch);
    g_free (self->priv->read_buffer);
    if (self->priv->timeout_source)
        g_source_remove (self->priv->timeout_source);

    G_OBJECT_CLASS (unity_system_compositor_parent_class)->finalize (object);
}

static void
unity_system_compositor_class_init (UnitySystemCompositorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    DisplayServerClass *display_server_class = DISPLAY_SERVER_CLASS (klass);

    display_server_class->get_vt = unity_system_compositor_get_vt;
    display_server_class->connect_session = unity_system_compositor_connect_session;
    display_server_class->disconnect_session = unity_system_compositor_disconnect_session;
    display_server_class->start = unity_system_compositor_start;
    display_server_class->stop = unity_system_compositor_stop;
    object_class->finalize = unity_system_compositor_finalize;

    g_type_class_add_private (klass, sizeof (UnitySystemCompositorPrivate));
}
