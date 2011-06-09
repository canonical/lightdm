#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <xcb/xcb.h>
#include <glib.h>

#include "status.h"

static void
quit_cb (int signum)
{
    notify_status ("SESSION TERMINATE SIGNAL=%d", signum);
    exit (EXIT_SUCCESS);
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    xcb_connection_t *connection;

    signal (SIGINT, quit_cb);
    signal (SIGTERM, quit_cb);

    notify_status ("SESSION START USER=%s", getenv ("USER"));

    loop = g_main_loop_new (NULL, FALSE);

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        fprintf (stderr, "Error connecting\n");
        return EXIT_FAILURE;
    }

    notify_status ("SESSION CONNECT-XSERVER");

    g_main_loop_run (loop);    

    return EXIT_SUCCESS;
}
