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

static GPid lightdm_pid = 0;
static gchar *status_socket_name = NULL;
static gboolean expect_exit = FALSE;
static GList *statuses = NULL;
static GList *script = NULL;
static GList *script_iter = NULL;
static guint status_timeout = 0;
static gboolean failed = FALSE;

static void check_status (const gchar *status);

static void
stop_daemon ()
{
    if (lightdm_pid)
        kill (lightdm_pid, SIGTERM);
}

static void
quit (int status)
{
    stop_daemon ();
    if (status_socket_name)
        unlink (status_socket_name);
    exit (status);
}

static void
fail (const gchar *event, const gchar *expected)
{
    GList *link;

    if (failed)
        return;
    failed = TRUE;

    for (link = statuses; link; link = link->next)
        g_printerr ("%s\n", (gchar *)link->data);
    if (event)
        g_printerr ("%s\n", event);
    if (expected)
        g_printerr ("^^^ expected \"%s\"\n", expected);
    else
        g_printerr ("^^^ expected nothing\n");

    stop_daemon ();
}

static gchar *
get_script_line ()
{
    if (!script_iter)
        return NULL;
    return script_iter->data;
}

static void
daemon_exit_cb (GPid pid, gint status, gpointer data)
{
    gchar *status_text;

    lightdm_pid = 0;
  
    if (WIFEXITED (status))
        status_text = g_strdup_printf ("RUNNER DAEMON-EXIT STATUS=%d", WEXITSTATUS (status));
    else
        status_text = g_strdup_printf ("RUNNER DAEMON-TERMINATE SIGNAL=%d", WTERMSIG (status));
    check_status (status_text);

    /* Check if expected more */
    if (get_script_line () != NULL)
    {
        fail ("(daemon quit)", get_script_line ());
        quit (EXIT_FAILURE);
    }

    if (expect_exit)
        quit (EXIT_SUCCESS);
    else
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

// FIXME: Add timeout

static void
run_commands ()
{
    /* Stop daemon if requested */
    while (TRUE)
    {
        gchar *command = get_script_line ();

        if (!command)
            break;

        /* Commands start with an asterisk */
        if (command[0] != '*')
            break;

        if (strcmp (command, "*STOP-DAEMON") == 0)
        {
            expect_exit = TRUE;
            stop_daemon ();
        }
        else
        {
            g_printerr ("Unknown command %s\n", command);
            quit (EXIT_FAILURE);
            return;
        }
        statuses = g_list_append (statuses, g_strdup (command));
        script_iter = script_iter->next;
    }

    /* Stop at the end of the script */
    if (get_script_line () == NULL)
    {
        expect_exit = TRUE;
        stop_daemon ();
    }
}

static gboolean
status_timeout_cb (gpointer data)
{
    fail ("(timeout)", get_script_line ());
    return FALSE;
}

static void
check_status (const gchar *status)
{
    gchar *pattern;

    if (failed)
        return;
  
    statuses = g_list_append (statuses, g_strdup (status));
  
    if (getenv ("DEBUG"))
        g_print ("%s\n", status);

    /* Try and match against expected */
    pattern = get_script_line ();
    if (!pattern || !g_regex_match_simple (pattern, status, 0, 0))
    {
        fail (NULL, pattern);
        return;
    }
    script_iter = script_iter->next;

    /* Restart timeout */
    g_source_remove (status_timeout);
    status_timeout = g_timeout_add (2000, status_timeout_cb, NULL);

    run_commands ();
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
        check_status ((gchar *) buffer);
    }

    return TRUE;
}

static void
signal_cb (int signum)
{
    g_debug ("Caught signal %d, killing daemon", signum);
    stop_daemon ();
}

static void
load_script (const gchar *name)
{
    int i;
    gchar *filename, *path, *data, **lines;

    filename = g_strdup_printf ("%s.script", name);
    path = g_build_filename ("scripts", filename, NULL);
    g_free (filename);

    if (!g_file_get_contents (path, &data, NULL, NULL))
    {
        g_printerr ("Unable to load script: %s\n", path);
        quit (EXIT_FAILURE);
    }
    g_free (path);

    lines = g_strsplit (data, "\n", -1);
    g_free (data);

    for (i = 0; lines[i]; i++)
    {
        gchar *line = g_strstrip (lines[i]);

        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#')
            continue;

        script = g_list_append (script, g_strdup (line));
    }
    script_iter = script;
    g_strfreev (lines);
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    gchar *script_name;
    int status_socket;
    gchar *command_line;
    gchar **lightdm_argv;
    gchar cwd[1024];
    GError *error = NULL;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    loop = g_main_loop_new (NULL, FALSE);

    if (argc != 2)
    {
        g_printerr ("Usage %s SCRIPT-NAME\n", argv[0]);
        quit (EXIT_FAILURE);
    }
    script_name = argv[1];

    load_script (script_name);
    
    g_debug ("Using script %s", script_name);

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

    run_commands ();

    status_timeout = g_timeout_add (2000, status_timeout_cb, NULL);

    command_line = g_strdup_printf ("../src/lightdm %s --no-root --config scripts/%s.conf --passwd-file data/test-passwd --theme-dir=data --theme-engine-dir=src/.libs --xsessions-dir=data",
                                    getenv ("DEBUG") ? "--debug" : "", script_name);
    g_debug ("Start daemon with command: %s", command_line);

    if (!g_shell_parse_argv (command_line, NULL, &lightdm_argv, &error))
    {
        g_warning ("Error parsing command line: %s", error->message);
        quit (EXIT_FAILURE);
    }
    g_clear_error (&error);

    if (!g_spawn_async (NULL, lightdm_argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &lightdm_pid, &error))
    {
        g_warning ("Error launching LightDM: %s", error->message);
        quit (EXIT_FAILURE);
    }
    g_clear_error (&error);

    g_child_watch_add (lightdm_pid, daemon_exit_cb, NULL);

    g_main_loop_run (loop);

    return EXIT_FAILURE;
}
