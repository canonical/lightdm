#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "status.h"

static GKeyFile *config;

int
main (int argc, char **argv)
{
    status_connect (NULL, NULL);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (g_key_file_get_boolean (config, "test-initctl-config", "report-events", NULL))
    {
        GString *status_text;
        int i;

        status_text = g_string_new ("INIT");
        for (i = 1; i < argc; i++)
            g_string_append_printf (status_text, " %s", argv[i]);
        status_notify (status_text->str);
        g_string_free (status_text, TRUE);
    }

    return EXIT_SUCCESS;
}
