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
#include "greeter-session.h"
#include "vt.h"

typedef struct
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

    /* Counters for Mir IDs to use */
    int next_session_id;
    int next_greeter_id;  
} UnitySystemCompositorPrivate;

static void unity_system_compositor_logger_iface_init (LoggerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (UnitySystemCompositor, unity_system_compositor, DISPLAY_SERVER_TYPE,
                         G_ADD_PRIVATE (UnitySystemCompositor)
                         G_IMPLEMENT_INTERFACE (LOGGER_TYPE, unity_system_compositor_logger_iface_init))

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
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);

    g_return_if_fail (compositor != NULL);
    g_return_if_fail (command != NULL);

    g_free (priv->command);
    priv->command = g_strdup (command);
}

void
unity_system_compositor_set_socket (UnitySystemCompositor *compositor, const gchar *socket)
{
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);
    g_return_if_fail (compositor != NULL);
    g_free (priv->socket);
    priv->socket = g_strdup (socket);
}

const gchar *
unity_system_compositor_get_socket (UnitySystemCompositor *compositor)
{
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);
    g_return_val_if_fail (compositor != NULL, NULL);
    return priv->socket;
}

void
unity_system_compositor_set_vt (UnitySystemCompositor *compositor, gint vt)
{
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);

    g_return_if_fail (compositor != NULL);

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
unity_system_compositor_set_timeout (UnitySystemCompositor *compositor, gint timeout)
{
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);
    g_return_if_fail (compositor != NULL);
    priv->timeout = timeout;
}

static void
write_message (UnitySystemCompositor *compositor, guint16 id, const guint8 *payload, guint16 payload_length)
{
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);

    gsize data_length = 4 + payload_length;
    g_autofree guint8 *data = g_malloc (data_length);
    data[0] = id >> 8;
    data[1] = id & 0xFF;
    data[2] = payload_length >> 8;
    data[3] = payload_length & 0xFF;
    if (payload)
        memcpy (data + 4, payload, payload_length);

    errno = 0;
    if (write (priv->to_compositor_pipe[1], data, data_length) != data_length)
        l_warning (compositor, "Failed to write to compositor: %s", strerror (errno));
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
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (UNITY_SYSTEM_COMPOSITOR (server));
    g_return_val_if_fail (server != NULL, 0);
    return priv->vt;
}

static void
unity_system_compositor_connect_session (DisplayServer *display_server, Session *session)
{
    UnitySystemCompositor *compositor = UNITY_SYSTEM_COMPOSITOR (display_server);
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);

    session_set_env (session, "XDG_SESSION_TYPE", "mir");

    if (priv->socket)
        session_set_env (session, "MIR_SERVER_HOST_SOCKET", priv->socket);

    if (!session_get_env (session, "MIR_SERVER_NAME"))
    {
        g_autofree gchar *name = NULL;
        if (IS_GREETER_SESSION (session))
        {
            name = g_strdup_printf ("greeter-%d", priv->next_greeter_id);
            priv->next_greeter_id++;
        }
        else
        {
            name = g_strdup_printf ("session-%d", priv->next_session_id);
            priv->next_session_id++;
        }
        session_set_env (session, "MIR_SERVER_NAME", name);
    }

    if (priv->vt >= 0)
    {
        g_autofree gchar *value = g_strdup_printf ("%d", priv->vt);
        session_set_env (session, "XDG_VTNR", value);
    }
}

static void
unity_system_compositor_disconnect_session (DisplayServer *display_server, Session *session)
{
    session_unset_env (session, "XDG_SESSION_TYPE");
    session_unset_env (session, "MIR_SERVER_HOST_SOCKET");
    session_unset_env (session, "MIR_SERVER_NAME");
    session_unset_env (session, "XDG_VTNR");
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

static gboolean
read_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    UnitySystemCompositor *compositor = data;
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);

    if (condition == G_IO_HUP)
    {
        l_debug (compositor, "Compositor closed communication channel");
        priv->from_compositor_watch = 0;
        return FALSE;
    }

    /* Work out how much required for a message */
    gsize n_to_read = 0;
    if (priv->read_buffer_n_used < 4)
        n_to_read = 4 - priv->read_buffer_n_used;
    else
    {
        guint16 payload_length = priv->read_buffer[2] << 8 | priv->read_buffer[3];
        n_to_read = 4 + payload_length - priv->read_buffer_n_used;
    }

    /* Read from compositor */
    if (n_to_read > 0)
    {
        gsize n_total = priv->read_buffer_n_used + n_to_read;
        if (priv->read_buffer_length < n_total)
            priv->read_buffer = g_realloc (priv->read_buffer, n_total);

        g_autoptr(GError) error = NULL;
        gsize n_read = 0;
        GIOStatus status = g_io_channel_read_chars (source,
                                                    (gchar *)priv->read_buffer + priv->read_buffer_n_used,
                                                    n_to_read,
                                                    &n_read,
                                                    &error);
        if (error)
            l_warning (compositor, "Failed to read from compositor: %s", error->message);
        if (status != G_IO_STATUS_NORMAL)
            return TRUE;
        priv->read_buffer_n_used += n_read;
    }

    /* Read header */
    if (priv->read_buffer_n_used < 4)
         return TRUE;
    guint16 id = priv->read_buffer[0] << 8 | priv->read_buffer[1];
    guint16 payload_length = priv->read_buffer[2] << 8 | priv->read_buffer[3];

    /* Read payload */
    if (priv->read_buffer_n_used < 4 + payload_length)
        return TRUE;
    /*guint8 *payload = priv->read_buffer + 4;*/

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
        if (!priv->is_ready)
        {
            priv->is_ready = TRUE;
            l_debug (compositor, "Compositor ready");
            g_source_remove (priv->timeout_source);
            priv->timeout_source = 0;
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
    priv->read_buffer_n_used = 0;

    return TRUE;
}

static void
run_cb (Process *process, gpointer user_data)
{
    /* Make input non-blocking */
    int fd = open ("/dev/null", O_RDONLY);
    dup2 (fd, STDIN_FILENO);
    close (fd);
}

static gboolean
timeout_cb (gpointer data)
{
    UnitySystemCompositor *compositor = data;
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);

    /* Stop the compositor - it is not working */
    display_server_stop (DISPLAY_SERVER (compositor));

    priv->timeout_source = 0;

    return TRUE;
}

static void
stopped_cb (Process *process, UnitySystemCompositor *compositor)
{
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);

    l_debug (compositor, "Unity system compositor stopped");

    if (priv->timeout_source != 0)
        g_source_remove (priv->timeout_source);
    priv->timeout_source = 0;

    /* Release VT and display number for re-use */
    if (priv->have_vt_ref)
    {
        vt_unref (priv->vt);
        priv->have_vt_ref = FALSE;
    }

    DISPLAY_SERVER_CLASS (unity_system_compositor_parent_class)->stop (DISPLAY_SERVER (compositor));
}

static gboolean
unity_system_compositor_start (DisplayServer *server)
{
    UnitySystemCompositor *compositor = UNITY_SYSTEM_COMPOSITOR (server);
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);

    g_return_val_if_fail (priv->process == NULL, FALSE);

    priv->is_ready = FALSE;

    g_return_val_if_fail (priv->command != NULL, FALSE);

    /* Create pipes to talk to compositor */
    if (pipe (priv->to_compositor_pipe) < 0 || pipe (priv->from_compositor_pipe) < 0)
    {
        l_debug (compositor, "Failed to create compositor pipes: %s", g_strerror (errno));
        return FALSE;
    }

    /* Don't allow the daemon end of the pipes to be accessed in the compositor */
    fcntl (priv->to_compositor_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl (priv->from_compositor_pipe[0], F_SETFD, FD_CLOEXEC);

    /* Listen for messages from the compositor */
    priv->from_compositor_channel = g_io_channel_unix_new (priv->from_compositor_pipe[0]);
    priv->from_compositor_watch = g_io_add_watch (priv->from_compositor_channel, G_IO_IN | G_IO_HUP, read_cb, compositor);

    /* Setup logging */
    g_autofree gchar *dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    g_autofree gchar *log_file = g_build_filename (dir, "unity-system-compositor.log", NULL);
    l_debug (compositor, "Logging to %s", log_file);

    /* Setup environment */
    priv->process = process_new (run_cb, compositor);
    gboolean backup_logs = config_get_boolean (config_get_instance (), "LightDM", "backup-logs");
    process_set_log_file (priv->process, log_file, TRUE, backup_logs ? LOG_MODE_BACKUP_AND_TRUNCATE : LOG_MODE_APPEND);
    process_set_clear_environment (priv->process, TRUE);
    process_set_env (priv->process, "XDG_SEAT", "seat0");
    g_autofree gchar *value = g_strdup_printf ("%d", priv->vt);
    process_set_env (priv->process, "XDG_VTNR", value);
    /* Variable required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
    {
        process_set_env (priv->process, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));
        process_set_env (priv->process, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
        process_set_env (priv->process, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
    }

    /* Generate command line to run */
    g_autofree gchar *absolute_command = get_absolute_command (priv->command);
    if (!absolute_command)
    {
        l_debug (compositor, "Can't launch compositor %s, not found in path", priv->command);
        return FALSE;
    }
    g_autoptr(GString) command = g_string_new (absolute_command);
    g_string_append_printf (command, " --file '%s'", priv->socket);
    g_string_append_printf (command, " --from-dm-fd %d --to-dm-fd %d", priv->to_compositor_pipe[0], priv->from_compositor_pipe[1]);
    if (priv->vt > 0)
        g_string_append_printf (command, " --vt %d", priv->vt);
    process_set_command (priv->process, command->str);

    /* Start the compositor */
    g_signal_connect (priv->process, PROCESS_SIGNAL_STOPPED, G_CALLBACK (stopped_cb), compositor);
    gboolean result = process_start (priv->process, FALSE);

    /* Close compositor ends of the pipes */
    close (priv->to_compositor_pipe[0]);
    priv->to_compositor_pipe[0] = -1;
    close (priv->from_compositor_pipe[1]);
    priv->from_compositor_pipe[1] = -1;

    if (!result)
        return FALSE;

    /* Connect to the compositor */
    if (priv->timeout > 0)
    {
        l_debug (compositor, "Waiting for system compositor for %ds", priv->timeout);
        priv->timeout_source = g_timeout_add (priv->timeout * 1000, timeout_cb, compositor);
    }

    return TRUE;
}

static void
unity_system_compositor_stop (DisplayServer *server)
{
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (UNITY_SYSTEM_COMPOSITOR (server));
    process_stop (priv->process);
}

static void
unity_system_compositor_init (UnitySystemCompositor *compositor)
{
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (compositor);
    priv->vt = -1;
    priv->command = g_strdup ("unity-system-compositor");
    priv->socket = g_strdup ("/run/mir_socket");
    priv->timeout = -1;
    priv->to_compositor_pipe[0] = -1;
    priv->to_compositor_pipe[1] = -1;
    priv->from_compositor_pipe[0] = -1;
    priv->from_compositor_pipe[1] = -1;
}

static void
unity_system_compositor_finalize (GObject *object)
{
    UnitySystemCompositor *self = UNITY_SYSTEM_COMPOSITOR (object);
    UnitySystemCompositorPrivate *priv = unity_system_compositor_get_instance_private (self);

    if (priv->process)
        g_signal_handlers_disconnect_matched (priv->process, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    g_clear_object (&priv->process);
    g_clear_pointer (&priv->command, g_free);
    g_clear_pointer (&priv->socket, g_free);
    if (priv->have_vt_ref)
        vt_unref (priv->vt);
    close (priv->to_compositor_pipe[0]);
    close (priv->to_compositor_pipe[1]);
    close (priv->from_compositor_pipe[0]);
    close (priv->from_compositor_pipe[1]);
    g_clear_pointer (&priv->from_compositor_channel, g_io_channel_unref);
    if (priv->from_compositor_watch)
        g_source_remove (priv->from_compositor_watch);
    g_clear_pointer (&priv->read_buffer, g_free);
    if (priv->timeout_source)
        g_source_remove (priv->timeout_source);

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
}

static gint
unity_system_compositor_real_logprefix (Logger *self, gchar *buf, gulong buflen)
{
    return g_snprintf (buf, buflen, "Unity System Compositor: ");
}

static void
unity_system_compositor_logger_iface_init (LoggerInterface *iface)
{
    iface->logprefix = &unity_system_compositor_real_logprefix;
}
