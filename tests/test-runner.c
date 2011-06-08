#include <stdlib.h>
#include <glib.h>

static void
daemon_exit_cb (GPid pid, gint status, gpointer data)
{
    if (WIFEXITED (status))
        g_debug ("LightDM daemon exited with return value %d", WEXITSTATUS (status));
    else
        g_debug ("LightDM daemon terminated with signal %d", WTERMSIG (status));
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    gchar *test_dir;
    gchar *command_line;
    gchar **lightdm_argv;
    GPid lightdm_pid;
    GError *error = NULL;

    loop = g_main_loop_new (NULL, FALSE);
  
    if (argc != 2)
    {
        g_printerr ("Usage %s TEST_DIRECTORY\n", argv[0]);
        return EXIT_FAILURE;
    }
    test_dir = argv[1];

    /* Only run the binaries we've built */
    g_setenv ("PATH", ".", TRUE);

    command_line = g_strdup_printf ("../src/lightdm --debug --no-root --config %s/lightdm.conf --theme-dir=%s --theme-engine-dir=.libs --xsessions-dir=%s",
                                    test_dir, test_dir, test_dir);
    g_debug ("Running %s", command_line);

    if (!g_shell_parse_argv (command_line, NULL, &lightdm_argv, &error))
    {
        g_warning ("Error parsing command line: %s", error->message);
        return EXIT_FAILURE;
    }
    g_clear_error (&error);

    if (!g_spawn_async (NULL, lightdm_argv, NULL, 0, NULL, NULL, &lightdm_pid, &error))
    {
        g_warning ("Error launching LightDM: %s", error->message);
        return EXIT_FAILURE;
    }
    g_clear_error (&error);

    g_child_watch_add (lightdm_pid, daemon_exit_cb, NULL);

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
