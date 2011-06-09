#include <stdlib.h>
#include <stdio.h>
#include <xcb/xcb.h>

#include "status.h"

int
main (int argc, char **argv)
{
    xcb_connection_t *connection;
  
    notify_status ("SESSION START");

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        fprintf (stderr, "Error connecting\n");
        return EXIT_FAILURE;
    }

    notify_status ("SESSION CONNECT-XSERVER");

    return EXIT_SUCCESS;
}
