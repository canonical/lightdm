#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>

#include "status.h"

static GKeyFile *config;

int
main (int argc, char **argv)
{
    g_type_init ();

    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    if (!g_key_file_get_boolean (config, "test-plymouth-config", "enabled", NULL))
        return EXIT_FAILURE;

    if (argc == 2 && strcmp (argv[1], "--ping") == 0)
    {
        if (g_key_file_get_boolean (config, "test-plymouth-config", "active", NULL))
        {
            notify_status ("PLYMOUTH PING ACTIVE=TRUE");
            return EXIT_SUCCESS;
        }
        else
        {
            notify_status ("PLYMOUTH PING ACTIVE=FALSE");
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && strcmp (argv[1], "--has-active-vt") == 0)
    {
        if (g_key_file_get_boolean (config, "test-plymouth-config", "has-active-vt", NULL))
        {          
            notify_status ("PLYMOUTH HAS-ACTIVE-VT=TRUE");
            return EXIT_SUCCESS;
        }
        else
        {
            notify_status ("PLYMOUTH HAS-ACTIVE-VT=FALSE");
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && strcmp (argv[1], "deactivate") == 0)
        notify_status ("PLYMOUTH DEACTIVATE");
    if (argc == 2 && strcmp (argv[1], "quit") == 0)
        notify_status ("PLYMOUTH QUIT RETAIN-SPLASH=FALSE");
    if (argc == 3 && strcmp (argv[1], "quit") == 0 && strcmp (argv[2], "--retain-splash") == 0)
        notify_status ("PLYMOUTH QUIT RETAIN-SPLASH=TRUE");

    return EXIT_SUCCESS;
}
