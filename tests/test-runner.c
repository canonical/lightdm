#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

/* For some reason sys/un.h doesn't define this */
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

static GPid lightdm_pid;
static gchar *status_socket_name = NULL;

static void
quit (int status)
{
    if (status_socket_name)
        unlink (status_socket_name);
    exit (status);
}

static void
daemon_exit_cb (GPid pid, gint status, gpointer data)
{
    if (WIFEXITED (status))
        g_debug ("LightDM daemon exited with return value %d", WEXITSTATUS (status));
    else
        g_debug ("LightDM daemon terminated with signal %d", WTERMSIG (status));

    /* FIXME: Not a failure if expecting it to quit */
    quit (EXIT_FAILURE);
}

static int
open_unix_socket (const gchar *name)
{
    int s;
    struct sockaddr_un address;

    s = socket (AF_UNIX, SOCK_DGRAM, 0);
    if (s < 0)
        return -1;
    address.sun_family = AF_UNIX;
    strncpy (address.sun_path, name, UNIX_PATH_MAX);
    if (bind (s, (struct sockaddr *) &address, sizeof (address)) < 0)
        return -1;
    return s;
}

static gboolean
status_message_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    int s;
    guint8 buffer[1024];
    ssize_t n_read;

    s = g_io_channel_unix_get_fd (channel);
    n_read = recv (s, buffer, 1023, 0);
    if (n_read < 0)
        g_warning ("Error reading from socket: %s", strerror (errno));
    else if (n_read == 0)
    {
        g_debug ("EOF");
        return FALSE;
    }
    else
    {
        buffer[n_read] = '\0';
        g_print ("%s\n", buffer);
    }

    return TRUE;
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    gchar *config;
    int status_socket;
    gchar *command_line;
    gchar **lightdm_argv;
    gchar cwd[1024];
    GError *error = NULL;

    loop = g_main_loop_new (NULL, FALSE);

    if (argc != 2)
    {
        g_printerr ("Usage %s CONFIG\n", argv[0]);
        quit (EXIT_FAILURE);
    }
    config = argv[1];

    /* Only run the binaries we've built */
    g_setenv ("PATH", ".", TRUE);

    if (!getcwd (cwd, 1024))
    {
        g_critical ("Error getting current directory: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }

    /* Open socket for status */
    status_socket_name = g_build_filename (cwd, ".status-socket", NULL);
    g_setenv ("LIGHTDM_TEST_STATUS_SOCKET", status_socket_name, TRUE);
    unlink (status_socket_name);  
    status_socket = open_unix_socket (status_socket_name);
    if (status_socket < 0)
    {
        g_critical ("Error opening status socket: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    g_io_add_watch (g_io_channel_unix_new (status_socket), G_IO_IN, status_message_cb, NULL);

    command_line = g_strdup_printf ("../src/lightdm --debug --no-root --config %s --theme-dir=%s --theme-engine-dir=%s/.libs --xsessions-dir=%s",
                                    config, cwd, cwd, cwd);
    g_debug ("Running %s", command_line);

    if (!g_shell_parse_argv (command_line, NULL, &lightdm_argv, &error))
    {
        g_warning ("Error parsing command line: %s", error->message);
        quit (EXIT_FAILURE);
    }
    g_clear_error (&error);

    if (!g_spawn_async (NULL, lightdm_argv, NULL, 0, NULL, NULL, &lightdm_pid, &error))
    {
        g_warning ("Error launching LightDM: %s", error->message);
        quit (EXIT_FAILURE);
    }
    g_clear_error (&error);

    g_child_watch_add (lightdm_pid, daemon_exit_cb, NULL);

    g_main_loop_run (loop);

    return EXIT_FAILURE;
}
