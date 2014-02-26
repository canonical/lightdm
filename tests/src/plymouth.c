#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>

#include "status.h"

static GKeyFile *config;

int
main (int argc, char **argv)
{
#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    status_connect (NULL, NULL);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (!g_key_file_get_boolean (config, "test-plymouth-config", "enabled", NULL))
        return EXIT_FAILURE;

    if (argc == 2 && strcmp (argv[1], "--ping") == 0)
    {
        if (g_key_file_get_boolean (config, "test-plymouth-config", "active", NULL))
        {
            status_notify ("PLYMOUTH PING ACTIVE=TRUE");
            return EXIT_SUCCESS;
        }
        else
        {
            status_notify ("PLYMOUTH PING ACTIVE=FALSE");
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && strcmp (argv[1], "--has-active-vt") == 0)
    {
        if (g_key_file_get_boolean (config, "test-plymouth-config", "has-active-vt", NULL))
        {          
            status_notify ("PLYMOUTH HAS-ACTIVE-VT=TRUE");
            return EXIT_SUCCESS;
        }
        else
        {
            status_notify ("PLYMOUTH HAS-ACTIVE-VT=FALSE");
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && strcmp (argv[1], "deactivate") == 0)
        status_notify ("PLYMOUTH DEACTIVATE");
    if (argc == 2 && strcmp (argv[1], "quit") == 0)
        status_notify ("PLYMOUTH QUIT RETAIN-SPLASH=FALSE");
    if (argc == 3 && strcmp (argv[1], "quit") == 0 && strcmp (argv[2], "--retain-splash") == 0)
        status_notify ("PLYMOUTH QUIT RETAIN-SPLASH=TRUE");

    return EXIT_SUCCESS;
}
