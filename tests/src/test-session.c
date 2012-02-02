#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <glib.h>
#include <glib-object.h>

#include "status.h"

static GKeyFile *config;

static void
quit_cb (int signum)
{
    notify_status ("SESSION %s TERMINATE SIGNAL=%d", getenv ("DISPLAY"), signum);
    exit (EXIT_SUCCESS);
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    xcb_connection_t *connection;
    gchar *logout_display;

    signal (SIGINT, quit_cb);
    signal (SIGTERM, quit_cb);

    g_type_init ();

    if (argc > 1)
        notify_status ("SESSION %s START NAME=%s USER=%s", getenv ("DISPLAY"), argv[1], getenv ("USER"));
    else
        notify_status ("SESSION %s START USER=%s", getenv ("DISPLAY"), getenv ("USER"));

    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    loop = g_main_loop_new (NULL, FALSE);

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        notify_status ("SESSION %s CONNECT-XSERVER-ERROR", getenv ("DISPLAY"));
        return EXIT_FAILURE;
    }

    notify_status ("SESSION %s CONNECT-XSERVER", getenv ("DISPLAY"));

    if (g_key_file_get_boolean (config, "test-session-config", "crash-xserver", NULL))
    {
        const gchar *name = "SIGSEGV";
        notify_status ("SESSION %s CRASH-XSERVER", getenv ("DISPLAY"));
        xcb_intern_atom (connection, FALSE, strlen (name), name);
        xcb_flush (connection);
    }

    logout_display = g_key_file_get_string (config, "test-session-config", "logout-display", NULL);
    if (logout_display && strcmp (logout_display, getenv ("DISPLAY")) == 0)
    {
        sleep (1);
        notify_status ("SESSION %s LOGOUT", getenv ("DISPLAY"));
        return EXIT_SUCCESS;
    }

    if (g_key_file_get_boolean (config, "test-session-config", "sigsegv", NULL))
    {
        notify_status ("SESSION %s CRASH", getenv ("DISPLAY"));
        kill (getpid (), SIGSEGV);
    }
  
    g_main_loop_run (loop);    

    return EXIT_SUCCESS;
}
