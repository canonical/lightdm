#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <xcb/xcb.h>

#include "status.h"

static void
quit_cb (int signum)
{
    notify_status ("SESSION QUIT");
    exit (EXIT_SUCCESS);
}

int
main (int argc, char **argv)
{
    xcb_connection_t *connection;

    signal (SIGINT, quit_cb);
    signal (SIGTERM, quit_cb);

    notify_status ("SESSION START USER=%s", getenv ("USER"));

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        fprintf (stderr, "Error connecting\n");
        return EXIT_FAILURE;
    }

    notify_status ("SESSION CONNECT-XSERVER");

    return EXIT_SUCCESS;
}
