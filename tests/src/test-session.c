#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <glib.h>

#include "status.h"

static GKeyFile *config;

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

    config = g_key_file_new ();
    if (g_getenv ("TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    loop = g_main_loop_new (NULL, FALSE);

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        fprintf (stderr, "Error connecting\n");
        return EXIT_FAILURE;
    }

    notify_status ("SESSION CONNECT-XSERVER");

    if (g_key_file_get_boolean (config, "test-session-config", "logout", NULL))
    {
        sleep (1);
        notify_status ("SESSION LOGOUT");
        return EXIT_SUCCESS;
    }

    if (g_key_file_get_boolean (config, "test-session-config", "sigsegv", NULL))
    {
        notify_status ("SESSION CRASH");
        kill (getpid (), SIGSEGV);
    }
  
    g_main_loop_run (loop);    

    return EXIT_SUCCESS;
}
