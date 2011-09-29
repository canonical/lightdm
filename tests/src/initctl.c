#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "status.h"

static GKeyFile *config;

int
main (int argc, char **argv)
{
    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    return EXIT_SUCCESS;
}
