#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>

#include "status.h"

static GKeyFile *config;

static void
request_cb (const gchar *message)
{
}

int
main (int argc, char **argv)
{
    g_type_init ();

    status_connect (request_cb);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);
  
    if (argc < 2)
    {
        g_printerr ("Usage: %s text [return-value]\n", argv[0]);
        return EXIT_FAILURE;
    }

    status_notify ("SCRIPT-HOOK %s", argv[1]);

    if (argc > 2)
        return atoi (argv[2]);
    else
        return EXIT_SUCCESS;
}
