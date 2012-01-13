#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib.h>

static gchar *
create_bus (const gchar *config_file, GPid *pid)
{
    int name_pipe[2];
    gchar *command, address[1024];
    gchar **argv;
    ssize_t n_read;
    GError *error = NULL;

    if (pipe (name_pipe) < 0)
    {
        g_warning ("Error creating pipe: %s", strerror (errno));
        exit (EXIT_FAILURE);
    }
    command = g_strdup_printf ("dbus-daemon --config-file=%s --print-address=%d", config_file, name_pipe[1]);
    if (!g_shell_parse_argv (command, NULL, &argv, &error))
    {
        g_warning ("Error parsing command line: %s", error->message);
        exit (EXIT_FAILURE);
    }
    g_clear_error (&error);
    if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN, NULL, NULL, pid, &error))
    {
        g_warning ("Error launching LightDM: %s", error->message);
        exit (EXIT_FAILURE);
    }

    n_read = read (name_pipe[0], address, 1023);
    if (n_read < 0)
    {
        g_warning ("Error reading D-Bus address: %s", strerror (errno));
        exit (EXIT_FAILURE);
    }
    address[n_read] = '\0';

    return g_strdup (address);
}

int
main (int argc, char **argv)
{
    gchar *conf_file, *system_bus_address, *session_bus_address;
    GPid system_bus_pid, session_bus_pid, child_pid;
    int status;

    conf_file = g_build_filename (SRCDIR, "system.conf", NULL);
    system_bus_address = create_bus (conf_file, &system_bus_pid);
    g_free (conf_file);
    g_setenv ("DBUS_SYSTEM_BUS_ADDRESS", g_strstrip (system_bus_address), TRUE);

    conf_file = g_build_filename (SRCDIR, "session.conf", NULL);
    session_bus_address = create_bus (conf_file, &session_bus_pid);
    g_free (conf_file);
    g_setenv ("DBUS_SESSION_BUS_ADDRESS", g_strstrip (session_bus_address), TRUE);

    child_pid = fork ();
    if (child_pid == 0)
    {
        execvp (argv[1], argv + 1);
        _exit (EXIT_FAILURE);
    }
    waitpid (child_pid, &status, 0);

    kill (session_bus_pid, SIGTERM);
    kill (system_bus_pid, SIGTERM);

    if (WIFEXITED (status))
        return WEXITSTATUS (status);
    else
        return EXIT_FAILURE;
}

