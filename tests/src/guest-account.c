#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
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
    gchar *passwd_path;

    g_type_init ();

    status_connect (request_cb);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    passwd_path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "etc", "passwd", NULL);

    if (argc == 2 && strcmp (argv[1], "add") == 0)
    {
        gchar *home_dir, *username, line[1024];
        gint max_uid = 1000;
        FILE *passwd;

        /* Create a unique name */
        home_dir = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "home", "guest-XXXXXX", NULL);
        if (!mkdtemp (home_dir))
        {
            g_printerr ("Failed to create home directory %s: %s\n", home_dir, strerror (errno));
            return EXIT_FAILURE;
        }
        username = strrchr (home_dir, '/') + 1;

        /* Get the largest UID */
        passwd = fopen (passwd_path, "r");
        if (passwd)
        {
            while (fgets (line, 1024, passwd))
            {
                gchar **tokens = g_strsplit (line, ":", -1);
                if (g_strv_length (tokens) >= 3)
                {
                    gint uid = atoi (tokens[2]);
                    if (uid > max_uid)
                        max_uid = uid;
                }
                g_strfreev (tokens);
            }
            fclose (passwd);
        }

        /* Add a new account to the passwd file */
        passwd = fopen (passwd_path, "a");
        fprintf (passwd, "%s::%d:%d:Guest Account:%s:/bin/sh\n", username, max_uid+1, max_uid+1, home_dir);
        fclose (passwd);

        status_notify ("GUEST-ACCOUNT ADD USERNAME=%s", username);

        /* Print out the username so LightDM picks it up */
        g_print ("%s\n", username);

        return EXIT_SUCCESS;
    }
    else if (argc == 3 && strcmp (argv[1], "remove") == 0)
    {
        gchar *username, *path, *prefix, line[1024];
        FILE *passwd, *new_passwd;

        username = argv[2];

        status_notify ("GUEST-ACCOUNT REMOVE USERNAME=%s", username);

        /* Open a new file for writing */
        passwd = fopen (passwd_path, "r");
        path = g_strdup_printf ("%s~", passwd_path);
        new_passwd = fopen (path, "w");

        /* Copy the old file, omitting our entry */
        prefix = g_strdup_printf ("%s:", username);
        while (fgets (line, 1024, passwd))
        {
            if (!g_str_has_prefix (line, prefix))
                fprintf (new_passwd, "%s", line);
        }
        fclose (passwd);
        fclose (new_passwd);

        /* Move the new file on the old one */
        rename (path, passwd_path);

        /* Delete home directory */
        gchar *command = g_strdup_printf ("rm -r %s/home/%s", g_getenv ("LIGHTDM_TEST_ROOT"), username);
        if (system (command))
            perror ("Failed to delete temp directory");

        return EXIT_SUCCESS;
    }

    g_printerr ("Usage %s add|remove\n", argv[0]);
    return EXIT_FAILURE;
}
