#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <glib.h>
#include <gio/gio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

/* For some reason sys/un.h doesn't define this */
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

/* Timeout in ms waiting for the status we expect */
#define STATUS_TIMEOUT 2000

/* Timeout in ms to wait for SIGTERM to kill compiz */
#define KILL_TIMEOUT 2000

static GKeyFile *config;
static gchar *status_socket_name = NULL;
static GList *statuses = NULL;
static GList *script = NULL;
static GList *script_iter = NULL;
static guint status_timeout = 0;
static gchar *temp_dir = NULL;
typedef struct
{
    pid_t pid;
    guint kill_timeout;
} Process;
static Process *lightdm_process = NULL, *bus_process = NULL;
static GHashTable *children = NULL;
static gboolean stop = FALSE;
static gint exit_status = 0;

static void quit (int status);
static void check_status (const gchar *status);

static gboolean
kill_timeout_cb (gpointer data)
{
    Process *process = data;

    if (getenv ("DEBUG"))
        g_print ("Sending SIGKILL to process %d\n", process->pid);
    kill (process->pid, SIGKILL);
    return FALSE;
}

static void
stop_process (Process *process)
{
    if (process->kill_timeout != 0)
        return;

    if (getenv ("DEBUG"))
        g_print ("Sending SIGTERM to process %d\n", process->pid);
    kill (process->pid, SIGTERM);
    process->kill_timeout = g_timeout_add (KILL_TIMEOUT, kill_timeout_cb, process);
}

static void
process_exit_cb (GPid pid, gint status, gpointer data)
{
    Process *process;
    gchar *status_text;
  
    if (getenv ("DEBUG"))
    {
        if (WIFEXITED (status))
            g_print ("Process %d exited with status %d\n", pid, WEXITSTATUS (status));
        else
            g_print ("Process %d terminated with signal %d\n", pid, WTERMSIG (status));
    }
  
    if (lightdm_process && pid == lightdm_process->pid)
    {
        process = lightdm_process;
        lightdm_process = NULL;
        if (WIFEXITED (status))
            status_text = g_strdup_printf ("RUNNER DAEMON-EXIT STATUS=%d", WEXITSTATUS (status));
        else
            status_text = g_strdup_printf ("RUNNER DAEMON-TERMINATE SIGNAL=%d", WTERMSIG (status));
        check_status (status_text);
    }
    else if (bus_process && pid == bus_process->pid)
    {
        process = bus_process;
        bus_process = NULL;
    }
    else
    {
        process = g_hash_table_lookup (children, GINT_TO_POINTER (pid));
        if (!process)
            return;
        g_hash_table_remove (children, GINT_TO_POINTER (pid));
    }

    if (process->kill_timeout)
        g_source_remove (process->kill_timeout);
    process->kill_timeout = 0;

    /* Quit once all children have stopped */
    if (stop)
        quit (exit_status);
}

static Process *
watch_process (pid_t pid)
{
    Process *process;  

    process = g_malloc0 (sizeof (Process));
    process->pid = pid;
    process->kill_timeout = 0;

    if (getenv ("DEBUG"))
        g_print ("Watching process %d\n", process->pid);
    g_child_watch_add (process->pid, process_exit_cb, NULL);

    return process;
}

static void
quit (int status)
{
    GHashTableIter iter;

    if (!stop)
        exit_status = status;
    stop = TRUE;

    /* Stop all the children */
    g_hash_table_iter_init (&iter, children);
    while (TRUE)
    {
        gpointer key, value;

        if (!g_hash_table_iter_next (&iter, &key, &value))
            break;

        stop_process ((Process *)value);
    }

    /* Don't quit until all children are stopped */
    if (g_hash_table_size (children) > 0)
        return;

    /* Stop the daemon */
    if (lightdm_process)
    {
        stop_process (lightdm_process);
        return;
    }

    /* Stop the bus */
    if (bus_process)
    {
        stop_process (bus_process);
        return;
    }

    if (status_socket_name)
        unlink (status_socket_name);

    if (temp_dir)
    {
        gchar *command = g_strdup_printf ("rm -r %s", temp_dir);
        if (system (command))
            perror ("Failed to delete temp directory");
    }

    exit (status);
}

static void
fail (const gchar *event, const gchar *expected)
{
    GList *link;

    if (stop)
        return;

    g_printerr ("Test failed, got the following events:\n");
    for (link = statuses; link; link = link->next)
        g_printerr ("    %s\n", (gchar *)link->data);
    if (event)
        g_printerr ("    %s\n", event);
    if (expected)
        g_printerr ("    ^^^ expected \"%s\"\n", expected);
    else
        g_printerr ("^^^ expected nothing\n");

    quit (EXIT_FAILURE);
}

static gchar *
get_script_line ()
{
    if (!script_iter)
        return NULL;
    return script_iter->data;
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

static void
run_commands ()
{
    /* Stop daemon if requested */
    while (TRUE)
    {
        gchar *command, *name = NULL, *c;
        GHashTable *params;

        command = get_script_line ();
        if (!command)
            break;

        /* Commands start with an asterisk */
        if (command[0] != '*')
            break;
        statuses = g_list_append (statuses, g_strdup (command));
        script_iter = script_iter->next;

        c = command + 1;
        while (*c && !isspace (*c))
            c++;
        name = g_strdup_printf ("%.*s", (int) (c - command - 1), command + 1);

        params = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
        while (TRUE)
        {
            gchar *start, *param_name, *param_value;
          
            while (isspace (*c))
                c++;
            start = c;
            while (*c && !isspace (*c) && *c != '=')
                c++;
            if (*c == '\0')
                break;

            param_name = g_strdup_printf ("%.*s", (int) (c - start), start);

            if (*c == '=')
            {
                c++;
                while (isspace (*c))
                    c++;
                if (*c == '\"')
                {
                    gboolean escaped = FALSE;
                    GString *value;

                    c++;
                    value = g_string_new ("");
                    while (*c)
                    {
                        if (*c == '\\')
                        {
                            if (escaped)
                            {
                                g_string_append_c (value, '\\');
                                escaped = FALSE;
                            }
                            else
                                escaped = TRUE;
                        }
                        else if (!escaped && *c == '\"')
                            break;
                        if (!escaped)
                            g_string_append_c (value, *c);
                        c++;
                    }
                    param_value = value->str;
                    g_string_free (value, FALSE);
                    if (*c == '\"')
                        c++;
                }
                else
                {
                    start = c;
                    while (*c && !isspace (*c))
                        c++;
                    param_value = g_strdup_printf ("%.*s", (int) (c - start), start);
                }
            }
            else
                param_value = g_strdup ("");

            g_hash_table_insert (params, param_name, param_value);
        }

        if (strcmp (name, "WAIT") == 0)
        {
            sleep (1);
        }
        else if (strcmp (name, "SWITCH-TO-GREETER") == 0)
        {
            g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL),
                                         "org.freedesktop.DisplayManager",
                                         "/org/freedesktop/DisplayManager/Seat0",
                                         "org.freedesktop.DisplayManager.Seat",
                                         "SwitchToGreeter",
                                         g_variant_new ("()"),
                                         G_VARIANT_TYPE ("()"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         1000,
                                         NULL,
                                         NULL);
            check_status ("RUNNER SWITCH-TO-GREETER");
        }
        else if (strcmp (name, "SWITCH-TO-USER") == 0)
        {
            gchar *status_text, *username;
          
            username = g_hash_table_lookup (params, "USERNAME");
            g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL),
                                         "org.freedesktop.DisplayManager",
                                         "/org/freedesktop/DisplayManager/Seat0",
                                         "org.freedesktop.DisplayManager.Seat",
                                         "SwitchToUser",
                                         g_variant_new ("(ss)", username, ""),
                                         G_VARIANT_TYPE ("()"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         1000,
                                         NULL,
                                         NULL);
            status_text = g_strdup_printf ("RUNNER SWITCH-TO-USER USERNAME=%s", username);
            check_status (status_text);
            g_free (status_text);
        }
        else if (strcmp (name, "SWITCH-TO-GUEST") == 0)
        {
            g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL),
                                         "org.freedesktop.DisplayManager",
                                         "/org/freedesktop/DisplayManager/Seat0",
                                         "org.freedesktop.DisplayManager.Seat",
                                         "SwitchToGuest",
                                         g_variant_new ("(s)", ""),
                                         G_VARIANT_TYPE ("()"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         1000,
                                         NULL,
                                         NULL);
            check_status ("RUNNER SWITCH-TO-GUEST");
        }
        else if (strcmp (name, "STOP-DAEMON") == 0)
            stop_process (lightdm_process);
        // FIXME: Make generic RUN-COMMAND
        else if (strcmp (name, "START-XSERVER") == 0)
        {
            gchar *xserver_args, *command_line;
            gchar **argv;
            GPid pid;
            Process *process;
            GError *error = NULL;

            xserver_args = g_hash_table_lookup (params, "ARGS");
            if (!xserver_args)
                xserver_args = "";
            command_line = g_strdup_printf ("%s/tests/src/X %s", BUILDDIR, xserver_args);

            if (!g_shell_parse_argv (command_line, NULL, &argv, &error) ||
                !g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error))
            {
                g_printerr ("Error starting X server: %s", error->message);
                quit (EXIT_FAILURE);
                return;
            }
            process = watch_process (pid);
            g_hash_table_insert (children, GINT_TO_POINTER (process->pid), process);
        }
        else if (strcmp (name, "START-VNC-CLIENT") == 0)
        {
            gchar *vnc_client_args, *command_line;
            gchar **argv;
            GPid pid;
            Process *process;
            GError *error = NULL;

            vnc_client_args = g_hash_table_lookup (params, "ARGS");
            if (!vnc_client_args)
                vnc_client_args = "";
            command_line = g_strdup_printf ("%s/tests/src/vnc-client %s", BUILDDIR, vnc_client_args);

            if (!g_shell_parse_argv (command_line, NULL, &argv, &error) ||
                !g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error))
            {
                g_printerr ("Error starting VNC client: %s", error->message);
                quit (EXIT_FAILURE);
                return;
            }
            process = watch_process (pid);
            g_hash_table_insert (children, GINT_TO_POINTER (process->pid), process);
        }
        else
        {
            g_printerr ("Unknown command '%s'\n", name);
            quit (EXIT_FAILURE);
            return;
        }

        g_free (name);
        g_hash_table_unref (params);
    }

    /* Stop at the end of the script */
    if (get_script_line () == NULL)
        quit (EXIT_SUCCESS);
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

    if (stop)
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
    status_timeout = g_timeout_add (STATUS_TIMEOUT, status_timeout_cb, NULL);

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
        return FALSE;
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
    g_print ("Caught signal %d, quitting\n", signum);
    quit (EXIT_FAILURE);
}

static void
load_script (const gchar *filename)
{
    int i;
    gchar *data, **lines;

    if (!g_file_get_contents (filename, &data, NULL, NULL))
    {
        g_printerr ("Unable to load script: %s\n", filename);
        quit (EXIT_FAILURE);
    }

    lines = g_strsplit (data, "\n", -1);
    g_free (data);

    /* Load lines with #? prefix as expected behaviour */
    for (i = 0; lines[i]; i++)
    {
        gchar *line = g_strstrip (lines[i]);
        if (g_str_has_prefix (line, "#?"))
            script = g_list_append (script, g_strdup (line+2));
    }
    script_iter = script;
    g_strfreev (lines);
}

static gchar *
create_bus (void)
{
    int name_pipe[2];
    gchar *command, address[1024];
    gchar **argv;
    GPid pid;
    ssize_t n_read;
    GError *error = NULL;

    if (pipe (name_pipe) < 0)
    {
        g_warning ("Error creating pipe: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    command = g_strdup_printf ("dbus-daemon --session --print-address=%d", name_pipe[1]);
    if (!g_shell_parse_argv (command, NULL, &argv, &error))
    {
        g_warning ("Error parsing command line: %s", error->message);
        quit (EXIT_FAILURE);
    }
    g_clear_error (&error);
    if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN, NULL, NULL, &pid, &error))
    {
        g_warning ("Error launching LightDM: %s", error->message);
        quit (EXIT_FAILURE);
    }
    bus_process = watch_process (pid);

    n_read = read (name_pipe[0], address, 1023);
    if (n_read < 0)
    {
        g_warning ("Error reading D-Bus address: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    address[n_read] = '\0';

    return g_strdup (address);
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    gchar *greeter = NULL, *script_name, *config_file, *config_path, *path, *path1, *path2, *ld_preload, *ld_library_path, *home_dir;
    GString *passwd_data;
    int status_socket;
    gchar *bus_address;
    GString *command_line;
    gchar **lightdm_argv;
    gchar cwd[1024];
    pid_t lightdm_pid;
    GError *error = NULL;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    g_type_init ();
  
    children = g_hash_table_new (g_direct_hash, g_direct_equal);

    loop = g_main_loop_new (NULL, FALSE);

    if (argc != 3)
    {
        g_printerr ("Usage %s SCRIPT-NAME GREETER\n", argv[0]);
        quit (EXIT_FAILURE);
    }
    script_name = argv[1];
    config_file = g_strdup_printf ("%s.conf", script_name);
    config_path = g_build_filename (SRCDIR, "tests", "scripts", config_file, NULL);
    g_free (config_file);

    /* Link to the correct greeter */
    path = g_build_filename (BUILDDIR, "tests", "default.desktop", NULL);
    if (unlink (path) < 0 && errno != ENOENT)
    {
        g_printerr ("Failed to rm greeter symlink %s: %s\n", path, strerror (errno));
        quit (EXIT_FAILURE);
    }

    greeter = g_strdup_printf ("%s.desktop", argv[2]);
    path1 = g_build_filename (SRCDIR, "tests", "data", "xgreeters", greeter, NULL);
    g_free(greeter);
    if (symlink (path1, path) < 0)
    {
        g_printerr ("Failed to make greeter symlink %s->%s: %s\n", path, path1, strerror (errno));
        quit (EXIT_FAILURE);
    }
    g_free (path);
    g_free (path1);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, config_path, G_KEY_FILE_NONE, NULL);

    load_script (config_path);

    /* Disable config if requested */
    if (g_key_file_has_key (config, "test-runner-config", "have-config", NULL) &&
        !g_key_file_get_boolean (config, "test-runner-config", "have-config", NULL))
        config_path = NULL;

    g_print ("----------------------------------------\n");
    g_print ("Running script %s\n", script_name);

    if (!getcwd (cwd, 1024))
    {
        g_critical ("Error getting current directory: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
  
    /* Don't contact our X server */
    g_unsetenv ("DISPLAY");

    /* Run local D-Bus daemons */
    bus_address = create_bus ();
    g_setenv ("DBUS_SYSTEM_BUS_ADDRESS", bus_address, TRUE);
    g_setenv ("DBUS_SESSION_BUS_ADDRESS", bus_address, TRUE);

    /* Override system calls */
    ld_preload = g_build_filename (BUILDDIR, "tests", "src", ".libs", "libsystem.so", NULL);
    g_setenv ("LD_PRELOAD", ld_preload, TRUE);
    g_free (ld_preload);

    /* Run test programs */
    path = g_strdup_printf ("%s/tests/src/.libs:%s/tests/src:%s/tests/src:%s", BUILDDIR, BUILDDIR, SRCDIR, g_getenv ("PATH"));
    g_setenv ("PATH", path, TRUE);
    g_free (path);

    /* Use locally built libraries */
    path1 = g_build_filename (BUILDDIR, "liblightdm-gobject", ".libs", NULL);  
    path2 = g_build_filename (BUILDDIR, "liblightdm-qt", ".libs", NULL);
    ld_library_path = g_strdup_printf ("%s:%s", path1, path2);
    g_free (path1);
    g_free (path2);
    g_setenv ("LD_LIBRARY_PATH", ld_library_path, TRUE);
    g_free (ld_library_path);

    /* Set config for child processes to read */
    if (config_path)
        g_setenv ("LIGHTDM_TEST_CONFIG", config_path, TRUE);

    /* Open socket for status */
    status_socket_name = g_build_filename (cwd, ".status-socket", NULL);
    g_setenv ("LIGHTDM_TEST_STATUS_SOCKET", status_socket_name, TRUE);
    unlink (status_socket_name);  
    status_socket = open_unix_socket (status_socket_name);
    if (status_socket < 0)
    {
        g_warning ("Error opening status socket: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    g_io_add_watch (g_io_channel_unix_new (status_socket), G_IO_IN, status_message_cb, NULL);

    /* Run from a temporary directory */
    temp_dir = g_build_filename (cwd, "lightdm-test-XXXXXX", NULL);
    if (!mkdtemp (temp_dir))
    {
        g_warning ("Error creating temporary directory: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    home_dir = g_build_filename (temp_dir, "home", NULL);
    g_setenv ("LIGHTDM_TEST_HOME_DIR", home_dir, TRUE);
    passwd_data = g_string_new ("");

    /* Make fake users */
    struct
    {
        gchar *user_name;
        gchar *password;
        gchar *real_name;
        gchar *dmrc;
        gint uid;
    } users[] =
    {
        {"root",    "",         "root",       NULL,  0},
        {"lightdm", "",         "",           NULL, 100},
        {"alice",   "password", "Alice User", NULL, 1000},
        {"bob",     "",         "Bob User",   NULL, 1001},
        {"carol",   "",         "Carol User", "[Desktop]\nSession=alternative\n", 1002},
        {NULL, NULL, 0}
    };
    int i;
    for (i = 0; users[i].user_name; i++)
    {
        path = g_build_filename (home_dir, users[i].user_name, NULL);
        g_mkdir_with_parents (path, 0755);
        g_free (path);

        if (users[i].dmrc)
        {
            path = g_build_filename (home_dir, users[i].user_name, ".dmrc", NULL);
            g_file_set_contents (path, users[i].dmrc, -1, NULL);
            g_free (path);
        }

        g_string_append_printf (passwd_data, "%s:%s:%d:%d:%s:%s/home/%s:/bin/sh\n", users[i].user_name, users[i].password, users[i].uid, users[i].uid, users[i].real_name, temp_dir, users[i].user_name);
    }
    path = g_build_filename (temp_dir, "passwd", NULL);
    g_setenv ("LIGHTDM_TEST_PASSWD_FILE", path, TRUE);
    g_file_set_contents (path, passwd_data->str, -1, NULL);
    g_free (path);
    g_string_free (passwd_data, TRUE);

    run_commands ();

    status_timeout = g_timeout_add (STATUS_TIMEOUT, status_timeout_cb, NULL);

    command_line = g_string_new ("../src/lightdm");
    if (getenv ("DEBUG"))
        g_string_append (command_line, " --debug");
    if (config_path)
        g_string_append_printf (command_line, " --config %s", config_path);
    g_string_append_printf (command_line, " --cache-dir %s/cache", temp_dir);
    g_string_append_printf (command_line, " --xsessions-dir=%s/tests/data/xsessions", SRCDIR);
    g_string_append_printf (command_line, " --xgreeters-dir=%s/tests", BUILDDIR);

    g_print ("Start daemon with command: PATH=%s LD_PRELOAD=%s LD_LIBRARY_PATH=%s LIGHTDM_TEST_STATUS_SOCKET=%s DBUS_SESSION_BUS_ADDRESS=%s %s\n",
             g_getenv ("PATH"), g_getenv ("LD_PRELOAD"), g_getenv ("LD_LIBRARY_PATH"), g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"), g_getenv ("DBUS_SESSION_BUS_ADDRESS"),
             command_line->str);

    if (!g_shell_parse_argv (command_line->str, NULL, &lightdm_argv, &error))
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
    lightdm_process = watch_process (lightdm_pid);

    check_status ("RUNNER DAEMON-START");

    g_main_loop_run (loop);

    return EXIT_FAILURE;
}
