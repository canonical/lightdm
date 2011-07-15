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
static gchar *temp_dir = NULL;
static GList *children = NULL;

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
    GList *link;

    stop_daemon ();
    if (status_socket_name)
        unlink (status_socket_name);

    for (link = children; link; link = link->next)
    {
        GPid pid = GPOINTER_TO_INT (link->data);
        kill (pid, SIGTERM);
    }

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

    if (failed)
        return;
    failed = TRUE;

    g_printerr ("Test failed, got the following events:\n");
    for (link = statuses; link; link = link->next)
        g_printerr ("    %s\n", (gchar *)link->data);
    if (event)
        g_printerr ("    %s\n", event);
    if (expected)
        g_printerr ("    ^^^ expected \"%s\"\n", expected);
    else
        g_printerr ("^^^ expected nothing\n");

    /* Either wait for the daemon to quit, or stop now if it already is */
    if (lightdm_pid)
        stop_daemon ();
    else
        quit (EXIT_FAILURE);
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

    /* Quit when the daemon does */
    if (failed)
        quit (EXIT_FAILURE);

    lightdm_pid = 0;
  
    if (WIFEXITED (status))
        status_text = g_strdup_printf ("RUNNER DAEMON-EXIT STATUS=%d", WEXITSTATUS (status));
    else
        status_text = g_strdup_printf ("RUNNER DAEMON-TERMINATE SIGNAL=%d", WTERMSIG (status));
    check_status (status_text);
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
        else if (strcmp (name, "SHOW-GREETER") == 0)
        {
            g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL),
                                         "org.freedesktop.DisplayManager",
                                         "/org/freedesktop/DisplayManager",                                         
                                         "org.freedesktop.DisplayManager",
                                         "ShowGreeter",
                                         g_variant_new ("()"),
                                         G_VARIANT_TYPE ("()"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         1000,
                                         NULL,
                                         NULL);
            check_status ("RUNNER SHOW-GREETER");
        }
        else if (strcmp (name, "SWITCH-TO-USER") == 0)
        {
            gchar *status_text, *username;
          
            username = g_hash_table_lookup (params, "USERNAME");
            g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL),
                                         "org.freedesktop.DisplayManager",
                                         "/org/freedesktop/DisplayManager",                                         
                                         "org.freedesktop.DisplayManager",
                                         "SwitchToUser",
                                         g_variant_new ("(s)", username),
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
                                         "/org/freedesktop/DisplayManager",                                         
                                         "org.freedesktop.DisplayManager",
                                         "SwitchToGuest",
                                         g_variant_new ("()"),
                                         G_VARIANT_TYPE ("()"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         1000,
                                         NULL,
                                         NULL);
            check_status ("RUNNER SWITCH-TO-GUEST");
        }
        else if (strcmp (name, "STOP-DAEMON") == 0)
        {
            expect_exit = TRUE;
            stop_daemon ();
        }
        else if (strcmp (name, "START-XSERVER") == 0)
        {
            gchar *xserver_args, *command_line;
            gchar **argv;
            GPid pid;
            GError *error = NULL;

            xserver_args = g_hash_table_lookup (params, "ARGS");
            if (!xserver_args)
                xserver_args = "";
            command_line = g_strdup_printf ("%s/tests/src/test-xserver %s", BUILDDIR, xserver_args);

            g_debug ("Run %s", command_line);
            if (!g_shell_parse_argv (command_line, NULL, &argv, &error) ||
                !g_spawn_async (NULL, argv, NULL, 0, NULL, NULL, &pid, &error))
            {
                g_printerr ("Error starting X server: %s", error->message);
                quit (EXIT_FAILURE);
                return;
            }
            children = g_list_append (children, GINT_TO_POINTER (pid));
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
    {
        if (lightdm_pid)
        {
            expect_exit = TRUE;
            stop_daemon ();
        }
        else
            quit (EXIT_SUCCESS);
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
    if (lightdm_pid != 0)
    {
        g_print ("Caught signal %d, killing daemon\n", signum);
        stop_daemon ();
    }
    else
    {
        g_print ("Caught signal %d, quitting\n", signum);
        quit (EXIT_FAILURE);
    }
}

static void
load_script (const gchar *name)
{
    int i;
    gchar *filename, *path, *data, **lines;

    filename = g_strdup_printf ("%s.script", name);
    path = g_build_filename (SRCDIR, "tests", "scripts", filename, NULL);
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
    gchar *script_name, *config_file, *config_path, *path, *path1, *path2, *ld_library_path, *home_dir;
    GString *passwd_data;
    int status_socket;
    gchar *dbus_command, dbus_address[1024];
    GPid pid;
    GString *command_line;
    int dbus_pipe[2];
    ssize_t n_read;
    gchar **dbus_argv;
    gchar **lightdm_argv;
    gchar cwd[1024];
    GError *error = NULL;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    g_type_init ();

    loop = g_main_loop_new (NULL, FALSE);

    if (argc != 2)
    {
        g_printerr ("Usage %s SCRIPT-NAME\n", argv[0]);
        quit (EXIT_FAILURE);
    }
    script_name = argv[1];
    config_file = g_strdup_printf ("%s.conf", script_name);
    config_path = g_build_filename (SRCDIR, "tests", "scripts", config_file, NULL);
    g_free (config_file);

    load_script (script_name);
    
    g_print ("----------------------------------------\n");
    g_print ("Running script %s\n", script_name);

    if (!getcwd (cwd, 1024))
    {
        g_critical ("Error getting current directory: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }

    /* Use locally built libraries and binaries */
    path = g_strdup_printf ("%s/tests/src/.libs:%s/tests/src:%s/tests/src:%s", BUILDDIR, BUILDDIR, SRCDIR, g_getenv ("PATH"));
    g_setenv ("PATH", path, TRUE);
    g_free (path);
    path1 = g_build_filename (BUILDDIR, "liblightdm-gobject", ".libs", NULL);  
    path2 = g_build_filename (BUILDDIR, "liblightdm-qt", "QLightDM", ".libs", NULL);
    ld_library_path = g_strdup_printf ("%s:%s", path1, path2);
    g_free (path1);
    g_free (path2);
    g_setenv ("LD_LIBRARY_PATH", ld_library_path, TRUE);
    g_free (ld_library_path);

    /* Set config for child processes to read */
    if (config_path)
        g_setenv ("LIGHTDM_TEST_CONFIG", config_path, TRUE);

    /* Run local D-Bus daemon */
    if (pipe (dbus_pipe) < 0)
    {
        g_warning ("Error creating pipe: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    dbus_command = g_strdup_printf ("dbus-daemon --session --print-address=%d", dbus_pipe[1]);
    if (!g_shell_parse_argv (dbus_command, NULL, &dbus_argv, &error))
    {
        g_warning ("Error parsing command line: %s", error->message);
        quit (EXIT_FAILURE);
    }
    g_clear_error (&error);
    if (!g_spawn_async (NULL, dbus_argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN, NULL, NULL, &pid, &error))
    {
        g_warning ("Error launching LightDM: %s", error->message);
        quit (EXIT_FAILURE);
    }
    children = g_list_append (children, GINT_TO_POINTER (pid));
    n_read = read (dbus_pipe[0], dbus_address, 1023);
    if (n_read < 0)
    {
        g_warning ("Error reading D-Bus address: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    dbus_address[n_read] = '\0';
    g_setenv ("DBUS_SESSION_BUS_ADDRESS", dbus_address, TRUE);

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

    /* Make fake users */
    temp_dir = g_build_filename (cwd, "lightdm-test-XXXXXX", NULL);
    if (!mkdtemp (temp_dir))
    {
        g_warning ("Error creating temporary directory: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    home_dir = g_build_filename (temp_dir, "home", NULL);
    g_setenv ("LIGHTDM_TEST_HOME_DIR", home_dir, TRUE);
    passwd_data = g_string_new ("");

    struct
    {
        gchar *user_name;
        gchar *password;
        gchar *real_name;
        gint uid;
    } users[] =
    {
        {"alice", "password", "Alice User", 1000},
        {"bob", "", "Bob User", 1001},
        {NULL, NULL, 0}
    };
    int i;
    for (i = 0; users[i].user_name; i++)
    {
        path = g_build_filename (home_dir, users[i].user_name, NULL);
        g_mkdir_with_parents (path, 0755);
        g_free (path);

        g_string_append_printf (passwd_data, "%s:%s:%d:%d:%s:%s/home/%s:/bin/sh\n", users[i].user_name, users[i].password, users[i].uid, users[i].uid, users[i].real_name, temp_dir, users[i].user_name);
    }

    path = g_build_filename (temp_dir, "passwd", NULL);
    g_setenv ("LIGHTDM_TEST_PASSWD_FILE", path, TRUE);
    g_file_set_contents (path, passwd_data->str, -1, NULL);
    g_free (path);
    g_string_free (passwd_data, TRUE);

    run_commands ();
  
    status_timeout = g_timeout_add (2000, status_timeout_cb, NULL);

    command_line = g_string_new ("../src/lightdm");
    if (getenv ("DEBUG"))
        g_string_append (command_line, " --debug");
    if (fopen (config_path, "r"))
        g_string_append_printf (command_line, " --config %s", config_path);
    g_string_append (command_line, " --no-root");
    g_string_append(command_line, " --default-xserver-command=test-xserver");
    g_string_append (command_line, " --default-xsession=test-session");
    g_string_append_printf (command_line, " --default-greeter-theme=test-theme");
    g_string_append_printf (command_line, " --passwd-file %s/passwd", temp_dir);
    g_string_append_printf (command_line, " --cache-dir %s/cache", temp_dir);
    g_string_append_printf (command_line, " --theme-dir=%s/tests/data/themes", SRCDIR);
    g_string_append_printf (command_line, " --theme-engine-dir=%s/tests/src/.libs", BUILDDIR);
    g_string_append_printf (command_line, " --xsessions-dir=%s/tests/data/xsessions", SRCDIR);
    g_string_append (command_line, " --minimum-display-number=50");

    g_print ("Start daemon with command: PATH=%s LD_LIBRARY_PATH=%s LIGHTDM_TEST_STATUS_SOCKET=%s DBUS_SESSION_BUS_ADDRESS=%s %s\n",
             g_getenv ("PATH"), g_getenv ("LD_LIBRARY_PATH"), g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"), g_getenv ("DBUS_SESSION_BUS_ADDRESS"),
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

    check_status ("RUNNER DAEMON-START");

    g_child_watch_add (lightdm_pid, daemon_exit_cb, NULL);

    g_main_loop_run (loop);

    return EXIT_FAILURE;
}
