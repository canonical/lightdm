#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <unistd.h>
#include <pwd.h>
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

/* Timeout in ms to wait for SIGTERM to be handled by a child process */
#define KILL_TIMEOUT 2000

static gchar *config_path;
static GKeyFile *config;
static gchar *status_socket_name = NULL;
static GList *statuses = NULL;
static GList *script = NULL;
static GList *script_iter = NULL;
static guint status_timeout = 0;
static gchar *temp_dir = NULL;
static int service_count;
typedef struct
{
    pid_t pid;
    guint kill_timeout;
} Process;
static Process *lightdm_process = NULL;
static GHashTable *children = NULL;
static gboolean stop = FALSE;
static gint exit_status = 0;
static GDBusConnection *accounts_connection = NULL;
static GDBusNodeInfo *accounts_info;
static GDBusNodeInfo *user_info;
typedef struct
{
    guint uid;
    gchar *username;
    guint id;
    gchar *xsession;
} AccountsUser;
static GList *accounts_users = NULL;
static void handle_user_call (GDBusConnection       *connection,
                              const gchar           *sender,
                              const gchar           *object_path,
                              const gchar           *interface_name,
                              const gchar           *method_name,
                              GVariant              *parameters,
                              GDBusMethodInvocation *invocation,
                              gpointer               user_data);
static GVariant *handle_user_get_property (GDBusConnection       *connection,
                                           const gchar           *sender,
                                           const gchar           *object_path,
                                           const gchar           *interface_name,
                                           const gchar           *property_name,
                                           GError               **error,
                                           gpointer               user_data);
static const GDBusInterfaceVTable user_vtable =
{
    handle_user_call,
    handle_user_get_property,
};

static void run_lightdm (void);
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
            g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
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
            g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
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
            g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
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

static void
handle_ck_call (GDBusConnection       *connection,
                const gchar           *sender,
                const gchar           *object_path,
                const gchar           *interface_name,
                const gchar           *method_name,
                GVariant              *parameters,
                GDBusMethodInvocation *invocation,
                gpointer               user_data)
{
    if (strcmp (method_name, "CanRestart") == 0)
    {
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
    }
    else if (strcmp (method_name, "CanStop") == 0)
    {
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
    }
    else if (strcmp (method_name, "CloseSession") == 0)
    {
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
    }
    else if (strcmp (method_name, "OpenSession") == 0)
    {
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", "deadbeef"));      
    }
    else if (strcmp (method_name, "OpenSessionWithParameters") == 0)
    {
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", "deadbeef"));      
    }
    else if (strcmp (method_name, "Restart") == 0)
    {
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "Stop") == 0)
    {
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
}

static void
ck_name_acquired_cb (GDBusConnection *connection,
                     const gchar     *name,
                     gpointer         user_data)
{
    const gchar *ck_interface =
        "<node>"
        "  <interface name='org.freedesktop.ConsoleKit.Manager'>"
        "    <method name='CanRestart'>"
        "      <arg name='can_restart' direction='out' type='b'/>"
        "    </method>"
        "    <method name='CanStop'>"
        "      <arg name='can_stop' direction='out' type='b'/>"
        "    </method>"
        "    <method name='CloseSession'>"
        "      <arg name='cookie' direction='in' type='s'/>"
        "      <arg name='result' direction='out' type='b'/>"
        "    </method>"
        "    <method name='OpenSession'>"
        "      <arg name='cookie' direction='out' type='s'/>"
        "    </method>"
        "    <method name='OpenSessionWithParameters'>"
        "      <arg name='parameters' direction='in' type='a(sv)'/>"
        "      <arg name='cookie' direction='out' type='s'/>"
        "    </method>"
        "    <method name='Restart'/>"
        "    <method name='Stop'/>"
        "    <signal name='SeatAdded'>"
        "      <arg name='seat' type='o'/>"
        "    </signal>"
        "    <signal name='SeatRemoved'>"
        "      <arg name='seat' type='o'/>"
        "    </signal>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable ck_vtable =
    {
        handle_ck_call,
    };
    GDBusNodeInfo *ck_info;
    GError *error = NULL;

    ck_info = g_dbus_node_info_new_for_xml (ck_interface, &error);
    if (error)
        g_warning ("Failed to parse D-Bus interface: %s", error->message);  
    g_clear_error (&error);
    if (!ck_info)
        return;
    g_dbus_connection_register_object (connection,
                                       "/org/freedesktop/ConsoleKit/Manager",
                                       ck_info->interfaces[0],
                                       &ck_vtable,
                                       NULL, NULL,
                                       &error);
    if (error)
        g_warning ("Failed to register console kit service: %s", error->message);
    g_clear_error (&error);
    g_dbus_node_info_unref (ck_info);

    service_count--;
    if (service_count == 0)
        run_lightdm ();
}

static void
start_console_kit_daemon ()
{
    service_count++;
    g_bus_own_name (G_BUS_TYPE_SYSTEM,
                    "org.freedesktop.ConsoleKit",
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    ck_name_acquired_cb,
                    NULL,
                    NULL,
                    NULL,
                    NULL);
}

static AccountsUser *
find_accounts_user (guint uid)
{
    GList *link;

    for (link = accounts_users; link; link = link->next)
    {
        AccountsUser *user = link->data;
        if (user->uid == uid)
            return user;
    }

    return NULL;
}

static void
handle_accounts_call (GDBusConnection       *connection,
                      const gchar           *sender,
                      const gchar           *object_path,
                      const gchar           *interface_name,
                      const gchar           *method_name,
                      GVariant              *parameters,
                      GDBusMethodInvocation *invocation,
                      gpointer               user_data)
{
    if (strcmp (method_name, "FindUserByName") == 0)
    {
        gchar *name, *data, **lines;
        int i;

        g_variant_get (parameters, "(&s)", &name);

        g_file_get_contents (g_getenv ("LIGHTDM_TEST_PASSWD_FILE"), &data, NULL, NULL);
        lines = g_strsplit (data, "\n", -1);
        g_free (data);

        for (i = 0; lines[i]; i++)
        {
            gchar **fields;
            fields = g_strsplit (lines[i], ":", -1);
            if (strcmp (fields[0], name) == 0)
            {
                gchar *path;
                GError *error = NULL;
                guint uid;
                AccountsUser *user;

                uid = atoi (fields[2]);
                path = g_strdup_printf ("/org/freedesktop/Accounts/User%d", uid);

                user = find_accounts_user (uid);
                if (!user)
                {
                    user = g_malloc0 (sizeof (AccountsUser));
                    accounts_users = g_list_append (accounts_users, user);

                    user->uid = uid;
                    user->username = g_strdup (name);
                    if (strcmp (name, "carol") == 0)
                        user->xsession = g_strdup ("alternative");
                    else
                        user->xsession = NULL;
                    user->id = g_dbus_connection_register_object (accounts_connection,
                                                                  path,
                                                                  user_info->interfaces[0],
                                                                  &user_vtable,
                                                                  user,
                                                                  NULL,
                                                                  &error);
                    if (error)
                        g_warning ("Failed to register user: %s", error->message);
                    g_clear_error (&error);
                }              

                g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", path));
                g_free (path);
                return;
            }
        }
      
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such user: %s", name);
    }
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such method: %s", method_name);    
}

static void
handle_user_call (GDBusConnection       *connection,
                  const gchar           *sender,
                  const gchar           *object_path,
                  const gchar           *interface_name,
                  const gchar           *method_name,
                  GVariant              *parameters,
                  GDBusMethodInvocation *invocation,
                  gpointer               user_data)
{
    AccountsUser *user = user_data;

    if (strcmp (method_name, "SetXSession") == 0)
    {
        gchar *xsession;

        g_variant_get (parameters, "(&s)", &xsession);

        g_free (user->xsession);
        user->xsession = g_strdup (xsession);

        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such method: %s", method_name);
}

static GVariant *
handle_user_get_property (GDBusConnection       *connection,
                          const gchar           *sender,
                          const gchar           *object_path,
                          const gchar           *interface_name,
                          const gchar           *property_name,
                          GError               **error,
                          gpointer               user_data)
{
    AccountsUser *user = user_data;

    if (strcmp (property_name, "BackgroundFile") == 0)
        return g_variant_new_string ("");
    else if (strcmp (property_name, "Language") == 0)
        return g_variant_new_string ("en_US");
    else if (strcmp (property_name, "XSession") == 0)
        return g_variant_new_string (user->xsession ? user->xsession : "");

    return NULL;
}

static void
accounts_name_acquired_cb (GDBusConnection *connection,
                           const gchar     *name,
                           gpointer         user_data)
{
    const gchar *accounts_interface =
        "<node>"
        "  <interface name='org.freedesktop.Accounts'>"
        "    <method name='FindUserByName'>"
        "      <arg name='name' direction='in' type='s'/>"
        "      <arg name='user' direction='out' type='o'/>"
        "    </method>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable accounts_vtable =
    {
        handle_accounts_call,
    };
    const gchar *user_interface =
        "<node>"
        "  <interface name='org.freedesktop.Accounts.User'>"
        "    <method name='SetXSession'>"
        "      <arg name='x_session' direction='in' type='s'/>"
        "    </method>"
        "    <property name='BackgroundFile' type='s' access='read'/>"
        "    <property name='Language' type='s' access='read'/>"
        "    <property name='XSession' type='s' access='read'/>"
        "  </interface>"
        "</node>";
    GError *error = NULL;

    accounts_connection = connection;

    accounts_info = g_dbus_node_info_new_for_xml (accounts_interface, &error);
    if (error)
        g_warning ("Failed to parse D-Bus interface: %s", error->message);  
    g_clear_error (&error);
    if (!accounts_info)
        return;
    user_info = g_dbus_node_info_new_for_xml (user_interface, &error);
    if (error)
        g_warning ("Failed to parse D-Bus interface: %s", error->message);  
    g_clear_error (&error);
    if (!user_info)
        return;
    g_dbus_connection_register_object (connection,
                                       "/org/freedesktop/Accounts",
                                       accounts_info->interfaces[0],
                                       &accounts_vtable,
                                       NULL,
                                       NULL,
                                       &error);
    if (error)
        g_warning ("Failed to register accounts service: %s", error->message);
    g_clear_error (&error);
    g_dbus_node_info_unref (accounts_info);

    service_count--;
    if (service_count == 0)
        run_lightdm ();
}

static void
start_accounts_service_daemon ()
{
    service_count++;
    g_bus_own_name (G_BUS_TYPE_SYSTEM,
                    "org.freedesktop.Accounts",
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    accounts_name_acquired_cb,
                    NULL,
                    NULL,
                    NULL,
                    NULL);
}

static void
run_lightdm ()
{
    GString *command_line;
    gchar **lightdm_argv;
    pid_t lightdm_pid;
    GError *error = NULL;

    run_commands ();

    status_timeout = g_timeout_add (STATUS_TIMEOUT, status_timeout_cb, NULL);

    command_line = g_string_new ("../src/lightdm");
    if (getenv ("DEBUG"))
        g_string_append (command_line, " --debug");
    if (!g_key_file_has_key (config, "test-runner-config", "have-config", NULL) ||
        g_key_file_get_boolean (config, "test-runner-config", "have-config", NULL))
    {
        g_string_append_printf (command_line, " --config %s", config_path);
        g_setenv ("LIGHTDM_TEST_CONFIG", config_path, TRUE);
    }
    g_string_append_printf (command_line, " --cache-dir %s/cache", temp_dir);
    g_string_append_printf (command_line, " --xsessions-dir=%s/usr/share/xsessions", temp_dir);
    g_string_append_printf (command_line, " --xgreeters-dir=%s/usr/share/xgreeters", temp_dir);

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
}

static gboolean
signal_cb (gpointer user_data)
{
    g_print ("Caught signal, quitting\n");
    quit (EXIT_FAILURE);
    return FALSE;
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    gchar *greeter = NULL, *script_name, *config_file, *path, *path1, *path2, *ld_preload, *ld_library_path, *home_dir;
    GString *passwd_data;
    int status_socket;
    gchar cwd[1024];

    g_type_init ();

    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGINT, signal_cb, NULL);
    g_unix_signal_add (SIGTERM, signal_cb, NULL);

    children = g_hash_table_new (g_direct_hash, g_direct_equal);

    if (argc != 3)
    {
        g_printerr ("Usage %s SCRIPT-NAME GREETER\n", argv[0]);
        quit (EXIT_FAILURE);
    }
    script_name = argv[1];
    config_file = g_strdup_printf ("%s.conf", script_name);
    config_path = g_build_filename (SRCDIR, "tests", "scripts", config_file, NULL);
    g_free (config_file);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, config_path, G_KEY_FILE_NONE, NULL);

    load_script (config_path);

    g_print ("----------------------------------------\n");
    g_print ("Running script %s\n", script_name);

    if (!getcwd (cwd, 1024))
    {
        g_critical ("Error getting current directory: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
  
    /* Don't contact our X server */
    g_unsetenv ("DISPLAY");

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

    /* Set up a skeleton file system */
    g_mkdir_with_parents (g_strdup_printf ("%s/etc", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/usr/share", temp_dir), 0755);

    /* Copy over the greeter files */
    if (system (g_strdup_printf ("cp -r %s/xsessions %s/usr/share", DATADIR, temp_dir)))
        perror ("Failed to copy xsessions");
    if (system (g_strdup_printf ("cp -r %s/xgreeters %s/usr/share", DATADIR, temp_dir)))
        perror ("Failed to copy xgreeters");

    /* Set up the default greeter */
    path = g_build_filename (temp_dir, "usr", "share", "xgreeters", "default.desktop", NULL);
    greeter = g_strdup_printf ("%s.desktop", argv[2]);
    if (symlink (greeter, path) < 0)
    {
        g_printerr ("Failed to make greeter symlink %s->%s: %s\n", path, greeter, strerror (errno));
        quit (EXIT_FAILURE);
    }
    g_free (path);
    g_free (greeter);

    home_dir = g_build_filename (temp_dir, "home", NULL);
    g_setenv ("LIGHTDM_TEST_HOME_DIR", home_dir, TRUE);

    /* Make fake users */
    struct
    {
        gchar *user_name;
        gchar *password;
        gboolean have_home_dir;
        gchar *real_name;
        gchar *xsession;
        gint uid;
    } users[] =
    {
        {"root",    "",         TRUE,  "root",       NULL,             0},
        {"lightdm", "",         TRUE,  "",           NULL,           100},
        {"alice",   "password", TRUE,  "Alice User", NULL,          1000},
        {"bob",     "",         TRUE,  "Bob User",   NULL,          1001},
        {"carol",   "",         TRUE,  "Carol User", "alternative", 1002},
        {"dave",    "",         FALSE, "Dave User",  NULL,          1003},
        {NULL,      NULL,       FALSE, NULL,         NULL,             0}
    };
    passwd_data = g_string_new ("");
    int i;
    for (i = 0; users[i].user_name; i++)
    {
        if (users[i].have_home_dir)
        {
            path = g_build_filename (home_dir, users[i].user_name, NULL);
            g_mkdir_with_parents (path, 0755);
            g_free (path);
        }

        if (users[i].xsession)
        {
            path = g_build_filename (home_dir, users[i].user_name, ".dmrc", NULL);
            g_file_set_contents (path, g_strdup_printf ("[Desktop]\nSession=%s", users[i].xsession), -1, NULL);
            g_free (path);
        }

        g_string_append_printf (passwd_data, "%s:%s:%d:%d:%s:%s/home/%s:/bin/sh\n", users[i].user_name, users[i].password, users[i].uid, users[i].uid, users[i].real_name, temp_dir, users[i].user_name);
    }
    path = g_build_filename (temp_dir, "passwd", NULL);
    g_setenv ("LIGHTDM_TEST_PASSWD_FILE", path, TRUE);
    g_file_set_contents (path, passwd_data->str, -1, NULL);
    g_free (path);
    g_string_free (passwd_data, TRUE);

    /* Start D-Bus services */
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-console-kit", NULL))
        start_console_kit_daemon ();
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-accounts-service", NULL))
        start_accounts_service_daemon ();

    g_main_loop_run (loop);

    return EXIT_FAILURE;
}
