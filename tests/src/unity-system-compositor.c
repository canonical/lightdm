#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <glib-unix.h>

#include "status.h"

static GMainLoop *loop;
static int exit_status = EXIT_SUCCESS;
static int from_dm_fd = -1, to_dm_fd = -1;

static GKeyFile *config;

static void
quit (int status)
{
    exit_status = status;
    g_main_loop_quit (loop);
}

static gboolean
sigint_cb (gpointer user_data)
{
    status_notify ("UNITY-SYSTEM-COMPOSITOR TERMINATE SIGNAL=%d", SIGINT);
    quit (EXIT_SUCCESS);
    return TRUE;
}

static gboolean
sigterm_cb (gpointer user_data)
{
    status_notify ("UNITY-SYSTEM-COMPOSITOR TERMINATE SIGNAL=%d", SIGTERM);
    quit (EXIT_SUCCESS);
    return TRUE;
}

typedef enum
{
   USC_MESSAGE_PING = 0,
   USC_MESSAGE_PONG = 1,
   USC_MESSAGE_READY = 2,
   USC_MESSAGE_SESSION_CONNECTED = 3,
   USC_MESSAGE_SET_ACTIVE_SESSION = 4,
   USC_MESSAGE_SET_NEXT_SESSION = 5,
} USCMessageID;

static void
write_message (guint16 id, const guint8 *payload, guint16 payload_length)
{
    guint8 *data;
    gsize data_length = 4 + payload_length;

    data = g_malloc (data_length);
    data[0] = id >> 8;
    data[1] = id & 0xFF;
    data[2] = payload_length >> 8;
    data[3] = payload_length & 0xFF;
    memcpy (data + 4, payload, payload_length);

    if (write (to_dm_fd, data, data_length) < 0)
        fprintf (stderr, "Failed to write to daemon: %s\n", strerror (errno));
}

static gboolean
read_message_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    gchar header[4], *payload;
    gsize n_read;
    guint16 id;
    guint16 payload_length;
    GError *error = NULL;

    if (g_io_channel_read_chars (channel, header, 4, &n_read, &error) != G_IO_STATUS_NORMAL)
    {
        g_printerr ("Failed to read header: %s\n", error->message);
        return FALSE;
    }
    if (n_read != 4)
    {
        g_printerr ("Short read for header, %zi instead of expected 4\n", n_read);
        return FALSE;
    }
    id = header[0] << 8 | header[1];
    payload_length = header[2] << 8 | header[3];
    payload = g_malloc0 (payload_length + 1);
    if (g_io_channel_read_chars (channel, payload, payload_length, &n_read, &error) != G_IO_STATUS_NORMAL)
    {
        g_printerr ("Failed to read payload: %s\n", error->message);
        return FALSE;
    }
    if (n_read != payload_length)
    {
        g_printerr ("Short read for payload, %zi instead of expected %d\n", n_read, payload_length);
        return FALSE;      
    }

    switch (id)
    {
    case USC_MESSAGE_PING:
        status_notify ("UNITY-SYSTEM-COMPOSITOR PING");
        break;
    case USC_MESSAGE_SET_ACTIVE_SESSION:
        status_notify ("UNITY-SYSTEM-COMPOSITOR SET-ACTIVE-SESSION ID=%s", (gchar *)payload);
        break;
    case USC_MESSAGE_SET_NEXT_SESSION:
        status_notify ("UNITY-SYSTEM-COMPOSITOR SET-NEXT-SESSION ID=%s", (gchar *)payload);
        break;
    default:
        g_printerr ("Ignoring message %d with %d octets\n", id, payload_length);
        break;
    }

    free (payload);

    return TRUE;
}

static void
request_cb (const gchar *request)
{
    if (!request)
    {
        g_main_loop_quit (loop);
        return;
    }

    if (strcmp (request, "UNITY-SYSTEM-COMPOSITOR PING") == 0)
        write_message (USC_MESSAGE_PING, NULL, 0);
    else if (strcmp (request, "UNITY-SYSTEM-COMPOSITOR PONG") == 0)
        write_message (USC_MESSAGE_PONG, NULL, 0);
    else if (strcmp (request, "UNITY-SYSTEM-COMPOSITOR READY") == 0)
        write_message (USC_MESSAGE_READY, NULL, 0);
}

int
main (int argc, char **argv)
{
    int i;
    GString *status_text;
    gboolean test = FALSE;
    int vt_number = -1;
    const gchar *file = NULL;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGINT, sigint_cb, NULL);
    g_unix_signal_add (SIGTERM, sigterm_cb, NULL);

    status_connect (request_cb);

    for (i = 1; i < argc; i++)
    {
        char *arg = argv[i];

        if (strcmp (arg, "--from-dm-fd") == 0)
        {
            from_dm_fd = atoi (argv[i+1]);
            i++;
        }
        else if (strcmp (arg, "--to-dm-fd") == 0)
        {
            to_dm_fd = atoi (argv[i+1]);
            i++;
        }
        else if (strcmp (arg, "--vt") == 0)
        {
            vt_number = atoi (argv[i+1]);
            i++;
        }
        else if (strcmp (arg, "--file") == 0)
        {
            file = argv[i+1];
            i++;
        }
        else if (strcmp (arg, "--test") == 0)
            test = TRUE;
        else
            return EXIT_FAILURE;
    }

    g_io_add_watch (g_io_channel_unix_new (from_dm_fd), G_IO_IN, read_message_cb, NULL);

    status_text = g_string_new ("UNITY-SYSTEM-COMPOSITOR START");
    if (file)
        g_string_append_printf (status_text, " FILE=%s", file);
    if (vt_number >= 0)
        g_string_append_printf (status_text, " VT=%d", vt_number);
    if (g_getenv ("XDG_VTNR"))
        g_string_append_printf (status_text, " XDG_VTNR=%s", g_getenv ("XDG_VTNR"));
    if (test)
        g_string_append (status_text, " TEST=TRUE");
    status_notify (status_text->str);
    g_string_free (status_text, TRUE);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (g_key_file_has_key (config, "unity-system-compositor-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "unity-system-compositor-config", "return-value", NULL);
        status_notify ("UNITY-SYSTEM-COMPOSITOR EXIT CODE=%d", return_value);
        return return_value;
    }

    g_main_loop_run (loop);

    return exit_status;
}
