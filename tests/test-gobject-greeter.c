#include <stdlib.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <lightdm/greeter.h>

static void connected_cb (LdmGreeter *greeter)
{
    printf ("CONNECTED\n");
}

int main (int argc, char **argv)
{
    LdmGreeter *greeter;
    xcb_connection_t *connection;

    g_type_init ();

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        fprintf (stderr, "Error connecting\n");
        return EXIT_FAILURE;
    }  

    greeter = ldm_greeter_new ();
    g_object_connect (greeter, "connected", G_CALLBACK (connected_cb), NULL);
    ldm_greeter_connect_to_server (greeter);

    g_main_loop_run (g_main_loop_new (NULL, FALSE));

    return EXIT_SUCCESS;
}
