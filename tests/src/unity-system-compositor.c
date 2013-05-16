#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "status.h"

static GMainLoop *loop;
static int exit_status = EXIT_SUCCESS;
static int from_dm_fd, to_dm_fd;

static void
quit (int status)
{
    exit_status = status;
    g_main_loop_quit (loop);
}

static void
signal_cb (int signum)
{
    status_notify ("UNITY-SYSTEM-COMPOSITOR TERMINATE SIGNAL=%d", signum);
    quit (EXIT_SUCCESS);
}

static void
request_cb (const gchar *request)
{
    if (!request)
    {
        g_main_loop_quit (loop);
        return;
    }
}

typedef enum
{
   USC_MESSAGE_PING = 0,
   USC_MESSAGE_PONG = 1,
   USC_MESSAGE_READY = 2,
   USC_MESSAGE_SESSION_CONNECTED = 3,
   USC_MESSAGE_SET_ACTIVE_SESSION = 4
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

int
main (int argc, char **argv)
{
    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);
    signal (SIGHUP, signal_cb);

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    loop = g_main_loop_new (NULL, FALSE);

    status_connect (request_cb);

    if (argc != 3)
        return EXIT_FAILURE;
    from_dm_fd = atoi (argv[1]);
    to_dm_fd = atoi (argv[2]);

    status_notify ("UNITY-SYSTEM-COMPOSITOR START");

    write_message (USC_MESSAGE_READY, NULL, 0);

    g_main_loop_run (loop);

    return exit_status;
}
