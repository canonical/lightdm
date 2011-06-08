#include <stdlib.h>
#include <stdio.h>
#include <xcb/xcb.h>

int
main (int argc, char **argv)
{
    xcb_connection_t *connection;

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        fprintf (stderr, "Error connecting\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
