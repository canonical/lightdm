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

    if (argc > 1)
        notify_status ("SESSION START NAME=%s USER=%s", argv[1], getenv ("USER"));
    else
        notify_status ("SESSION START USER=%s", getenv ("USER"));

    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    loop = g_main_loop_new (NULL, FALSE);

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        notify_status ("SESSION CONNECT-XSERVER-ERROR");
        return EXIT_FAILURE;
    }

    notify_status ("SESSION CONNECT-XSERVER");

    if (g_key_file_get_boolean (config, "test-session-config", "crash-xserver", NULL))
    {
        const gchar *name = "SIGSEGV";
        notify_status ("SESSION CRASH-XSERVER");
        xcb_intern_atom (connection, FALSE, strlen (name), name);
        xcb_flush (connection);
    }

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
