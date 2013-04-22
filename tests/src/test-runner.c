#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <unistd.h>
#include <pwd.h>

/* Timeout in ms waiting for the status we expect */
#define STATUS_TIMEOUT 2000

/* Timeout in ms to wait for SIGTERM to be handled by a child process */
#define KILL_TIMEOUT 2000

static gchar *test_runner_command;
static gchar *config_path;
static GKeyFile *config;
static GSocket *status_socket = NULL;
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
    gchar *user_name;
    gchar *real_name;
    gchar *home_directory;
    gchar *path;
    guint id;
    gchar *language;
    gchar *xsession;
    gchar **layouts;
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
static GDBusConnection *ck_connection = NULL;
static GDBusNodeInfo *ck_session_info;
typedef struct
{
    gchar *cookie;
    gchar *path;
    guint id;
} CKSession;
static GList *ck_sessions = NULL;
static gint ck_session_index = 0;
static void handle_session_call (GDBusConnection       *connection,
                                    const gchar           *sender,
                                    const gchar           *object_path,
                                    const gchar           *interface_name,
                                    const gchar           *method_name,
                                    GVariant              *parameters,
                                    GDBusMethodInvocation *invocation,
                                    gpointer               user_data);
static const GDBusInterfaceVTable ck_session_vtable =
{
    handle_session_call,
};

typedef struct
{
    gchar *path;
    guint pid;
} LogindSession;

static GList *logind_sessions = NULL;
static gint logind_session_index = 0;

typedef struct
{
    GSocket *socket;
    GSource *source;
} StatusClient;
static GList *status_clients = NULL;

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
        gchar *command = g_strdup_printf ("rm -rf %s", temp_dir);
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

    g_printerr ("Command line: %s", test_runner_command);
    g_printerr ("Events:\n");
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

static const gchar *
get_script_line ()
{
    if (!script_iter)
        return NULL;
    return script_iter->data;
}

static void
handle_command (const gchar *command)
{
    const gchar *c;
    gchar *name = NULL;
    GHashTable *params;

    c = command;
    while (*c && !isspace (*c))
        c++;
    name = g_strdup_printf ("%.*s", (int) (c - command), command);

    params = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    while (TRUE)
    {
        const gchar *start;
        gchar *param_name, *param_value;

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
        }
        else
        {
            process = watch_process (pid);
            g_hash_table_insert (children, GINT_TO_POINTER (process->pid), process);
        }
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
        }
        else
        {
            process = watch_process (pid);
            g_hash_table_insert (children, GINT_TO_POINTER (process->pid), process);
        }
    }
    /* Forward to external processes */
    else if (strcmp (name, "SESSION") == 0 ||
             strcmp (name, "GREETER") == 0 ||
             strcmp (name, "XSERVER") == 0)
    {
        GList *link;
        for (link = status_clients; link; link = link->next)
        {
            StatusClient *client = link->data;
            int length;
            GError *error = NULL;
      
            length = strlen (command);
            g_socket_send (client->socket, (gchar *) &length, sizeof (length), NULL, &error);
            g_socket_send (client->socket, command, strlen (command), NULL, &error);
            if (error)
                g_printerr ("Failed to write to client socket: %s\n", error->message);
            g_clear_error (&error);
        }     
    }
    else
    {
        g_printerr ("Unknown command '%s'\n", name);
        quit (EXIT_FAILURE);
    }
  
    g_free (name);
    g_hash_table_unref (params);
}

static void
run_commands ()
{
    /* Stop daemon if requested */
    while (TRUE)
    {
        const gchar *command;

        /* Commands start with an asterisk */
        command = get_script_line ();
        if (!command || command[0] != '*')
            break;

        statuses = g_list_append (statuses, g_strdup (command));
        script_iter = script_iter->next;

        handle_command (command + 1);
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
    const gchar *pattern;
    gboolean result = FALSE;

    if (stop)
        return;
  
    statuses = g_list_append (statuses, g_strdup (status));
  
    if (getenv ("DEBUG"))
        g_print ("%s\n", status);

    /* Try and match against expected */
    pattern = get_script_line ();
    if (pattern)
    {
        gchar *full_pattern = g_strdup_printf ("^%s$", pattern);
        result = g_regex_match_simple (full_pattern, status, 0, 0);
        g_free (full_pattern);
    }
  
    if (!result)
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
status_message_cb (GSocket *socket, GIOCondition condition, StatusClient *client)
{
    int length;
    gchar buffer[1024];
    ssize_t n_read;
    GError *error = NULL;

    n_read = g_socket_receive (socket, (gchar *)&length, sizeof (length), NULL, &error);
    if (n_read > 0)
        n_read = g_socket_receive (socket, buffer, length, NULL, &error);
    if (error)
        g_warning ("Error reading from socket: %s", error->message);
    g_clear_error (&error);
    if (n_read == 0)
    {
        status_clients = g_list_remove (status_clients, client);
        g_object_unref (client->socket);
        g_free (client);
        return FALSE;
    }
    else if (n_read > 0)
    {
        buffer[n_read] = '\0';
        check_status (buffer);
    }

    return TRUE;
}

static gboolean
status_connect_cb (gpointer data)
{
    GSocket *socket;
    GError *error = NULL;

    socket = g_socket_accept (status_socket, NULL, &error);
    if (error)
        g_warning ("Failed to accept status connection: %s", error->message);
    g_clear_error (&error);
    if (socket)
    {
        StatusClient *client;

        client = g_malloc0 (sizeof (StatusClient));
        client->socket = socket;
        client->source = g_socket_create_source (socket, G_IO_IN, NULL);
        status_clients = g_list_append (status_clients, client);

        g_source_set_callback (client->source, (GSourceFunc) status_message_cb, client, NULL);
        g_source_attach (client->source, NULL);
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
handle_upower_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
    if (strcmp (method_name, "SuspendAllowed") == 0)
    {
        check_status ("UPOWER SUSPEND-ALLOWED");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
    }
    else if (strcmp (method_name, "Suspend") == 0)
    {
        check_status ("UPOWER SUSPEND");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "HibernateAllowed") == 0)
    {
        check_status ("UPOWER HIBERNATE-ALLOWED");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
    }
    else if (strcmp (method_name, "Hibernate") == 0)
    {
        check_status ("UPOWER HIBERNATE");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such method: %s", method_name);
}

static void
upower_name_acquired_cb (GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
    const gchar *upower_interface =
        "<node>"
        "  <interface name='org.freedesktop.UPower'>"
        "    <method name='SuspendAllowed'>"
        "      <arg name='allowed' direction='out' type='b'/>"
        "    </method>"
        "    <method name='Suspend'/>"
        "    <method name='HibernateAllowed'>"
        "      <arg name='allowed' direction='out' type='b'/>"
        "    </method>"
        "    <method name='Hibernate'/>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable upower_vtable =
    {
        handle_upower_call,
    };
    GDBusNodeInfo *upower_info;
    GError *error = NULL;

    upower_info = g_dbus_node_info_new_for_xml (upower_interface, &error);
    if (error)
        g_warning ("Failed to parse D-Bus interface: %s", error->message);  
    g_clear_error (&error);
    if (!upower_info)
        return;
    g_dbus_connection_register_object (connection,
                                       "/org/freedesktop/UPower",
                                       upower_info->interfaces[0],
                                       &upower_vtable,
                                       NULL, NULL,
                                       &error);
    if (error)
        g_warning ("Failed to register UPower service: %s", error->message);
    g_clear_error (&error);
    g_dbus_node_info_unref (upower_info);

    service_count--;
    if (service_count == 0)
        run_lightdm ();
}

static void
start_upower_daemon ()
{
    service_count++;
    g_bus_own_name (G_BUS_TYPE_SYSTEM,
                    "org.freedesktop.UPower",
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    upower_name_acquired_cb,
                    NULL,
                    NULL,
                    NULL,
                    NULL);
}

static CKSession *
open_ck_session (GVariant *params)
{
    CKSession *session;
    GString *cookie;
    GVariantIter *iter;
    const gchar *name;
    GVariant *value;
    GError *error = NULL;

    session = g_malloc0 (sizeof (CKSession));
    ck_sessions = g_list_append (ck_sessions, session);

    cookie = g_string_new ("ck-cookie");
    g_variant_get (params, "a(sv)", &iter);
    while (g_variant_iter_loop (iter, "(&sv)", &name, &value))
    {
        if (strcmp (name, "x11-display") == 0)
        {
            const gchar *display;
            g_variant_get (value, "&s", &display);
            g_string_append_printf (cookie, "-x%s", display);
        }
    }

    session->cookie = cookie->str;
    g_string_free (cookie, FALSE);
    session->path = g_strdup_printf ("/org/freedesktop/ConsoleKit/Session%d", ck_session_index++);
    session->id = g_dbus_connection_register_object (ck_connection,
                                                     session->path,
                                                     ck_session_info->interfaces[0],
                                                     &ck_session_vtable,
                                                     session,
                                                     NULL,
                                                     &error);
    if (error)
        g_warning ("Failed to register CK Session: %s", error->message);
    g_clear_error (&error);

    return session;
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
        check_status ("CONSOLE-KIT CAN-RESTART");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
    }
    else if (strcmp (method_name, "CanStop") == 0)
    {
        check_status ("CONSOLE-KIT CAN-STOP");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
    }
    else if (strcmp (method_name, "CloseSession") == 0)
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
    else if (strcmp (method_name, "OpenSession") == 0)
    {
        GVariantBuilder params;
        g_variant_builder_init (&params, G_VARIANT_TYPE ("a(sv)"));
        CKSession *session = open_ck_session (g_variant_builder_end (&params));
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", session->cookie));
    }
    else if (strcmp (method_name, "OpenSessionWithParameters") == 0)
    {
        CKSession *session = open_ck_session (g_variant_get_child_value (parameters, 0));
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", session->cookie));
    }
    else if (strcmp (method_name, "GetSessionForCookie") == 0)
    {
        GList *link;
        gchar *cookie;

        g_variant_get (parameters, "(&s)", &cookie);

        for (link = ck_sessions; link; link = link->next)
        {
            CKSession *session = link->data;
            if (strcmp (session->cookie, cookie) != 0)
            {
                g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", session->path));
                return;
            }
        }

        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Unable to find session for cookie");
    }
    else if (strcmp (method_name, "Restart") == 0)
    {
        check_status ("CONSOLE-KIT RESTART");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "Stop") == 0)
    {
        check_status ("CONSOLE-KIT STOP");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such method: %s", method_name);
}


// Shared between CK and Logind - identical signatures
static void
handle_session_call (GDBusConnection       *connection,
                        const gchar           *sender,
                        const gchar           *object_path,
                        const gchar           *interface_name,
                        const gchar           *method_name,
                        GVariant              *parameters,
                        GDBusMethodInvocation *invocation,
                        gpointer               user_data)
{
    if (strcmp (method_name, "Lock") == 0)
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    else if (strcmp (method_name, "Unlock") == 0)
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such method: %s", method_name);
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
        "    <method name='GetSessionForCookie'>"
        "      <arg name='cookie' direction='in' type='s'/>"
        "      <arg name='ssid' direction='out' type='o'/>"
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
    const gchar *ck_session_interface =
        "<node>"
        "  <interface name='org.freedesktop.ConsoleKit.Session'>"
        "    <method name='Lock'/>"
        "    <method name='Unlock'/>"
        "  </interface>"
        "</node>";
    GDBusNodeInfo *ck_info;
    GError *error = NULL;

    ck_connection = connection;

    ck_info = g_dbus_node_info_new_for_xml (ck_interface, &error);
    if (error)
        g_warning ("Failed to parse D-Bus interface: %s", error->message);  
    g_clear_error (&error);
    if (!ck_info)
        return;
    ck_session_info = g_dbus_node_info_new_for_xml (ck_session_interface, &error);
    if (error)
        g_warning ("Failed to parse D-Bus interface: %s", error->message);  
    g_clear_error (&error);
    if (!ck_session_info)
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

static LogindSession *
open_logind_session (GDBusConnection *connection,
		     GVariant *params)
{
    LogindSession *session;
    GError *error = NULL;
    GDBusNodeInfo *logind_session_info;

    const gchar *logind_session_interface =
        "<node>"
        "  <interface name='org.freedesktop.login1.Session'>"
        "    <method name='Lock'/>"
        "    <method name='Unlock'/>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable logind_session_vtable =
    {
	handle_session_call,
    };

    session = g_malloc0 (sizeof (LogindSession));
    logind_sessions = g_list_append (logind_sessions, session);

    session->path = g_strdup_printf("/org/freedesktop/login1/Session/c%d",
				    logind_session_index++);



    logind_session_info = g_dbus_node_info_new_for_xml (logind_session_interface,
							&error);
    if (error)
        g_warning ("Failed to parse logind session D-Bus interface: %s",
		   error->message);  
    g_clear_error (&error);
    if (!logind_session_info)
        return;

    g_dbus_connection_register_object (connection,
				       session->path,
				       logind_session_info->interfaces[0],
				       &logind_session_vtable,
				       session,
				       NULL,
				       &error);
    if (error)
        g_warning ("Failed to register logind session: %s", error->message);
    g_clear_error (&error);
    g_dbus_node_info_unref (logind_session_info);

    return session;
}


static void
handle_login1_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{

    if (strcmp (method_name, "GetSessionByPID") == 0)
    {
	//Look for a session with our PID, and create one if we don't have one
	//already.
	GList *link;
	guint pid;
	LogindSession *ret = NULL;

	g_variant_get (parameters, "(u)", &pid);

	for (link = logind_sessions; link; link = link->next)
	{
	    LogindSession *session;
	    session = link->data;
	    if (session->pid == pid)
	    {
		ret = session;
		break;
	    }
	}
	// Not found
	if (!ret)
	    ret = open_logind_session (connection, parameters);

	g_dbus_method_invocation_return_value (invocation,
					       g_variant_new("(o)", ret->path));
	
    }
    else if (strcmp (method_name, "CanReboot") == 0)
    {
        check_status ("LOGIN1 CAN-REBOOT");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", "yes"));
    }
    else if (strcmp (method_name, "Reboot") == 0)
    {
        gboolean interactive;
        g_variant_get (parameters, "(b)", &interactive);
        check_status ("LOGIN1 REBOOT");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "CanPowerOff") == 0)
    {
        check_status ("LOGIN1 CAN-POWER-OFF");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", "yes"));
    }
    else if (strcmp (method_name, "Suspend") == 0)
    {
        gboolean interactive;
        g_variant_get (parameters, "(b)", &interactive);
        check_status ("LOGIN1 SUSPEND");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "CanSuspend") == 0)
    {
        check_status ("LOGIN1 CAN-SUSPEND");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", "yes"));
    }
    else if (strcmp (method_name, "PowerOff") == 0)
    {
        gboolean interactive;
        g_variant_get (parameters, "(b)", &interactive);
        check_status ("LOGIN1 POWER-OFF");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "CanHibernate") == 0)
    {
        check_status ("LOGIN1 CAN-HIBERNATE");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", "yes"));
    }
    else if (strcmp (method_name, "Hibernate") == 0)
    {
        gboolean interactive;
        g_variant_get (parameters, "(b)", &interactive);
        check_status ("LOGIN1 HIBERNATE");
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such method: %s", method_name);
}

static void
login1_name_acquired_cb (GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
    const gchar *login1_interface =
        "<node>"
        "  <interface name='org.freedesktop.login1.Manager'>"
        "    <method name='GetSessionByPID'>"
        "      <arg name='pid' type='u' direction='in'/>"
        "      <arg name='session' type='o' direction='out'/>"
        "    </method>"
        "    <method name='CanReboot'>"
        "      <arg name='result' direction='out' type='s'/>"
        "    </method>"
        "    <method name='Reboot'>"
        "      <arg name='interactive' direction='in' type='b'/>"
        "    </method>"
        "    <method name='CanPowerOff'>"
        "      <arg name='result' direction='out' type='s'/>"
        "    </method>"
        "    <method name='PowerOff'>"
        "      <arg name='interactive' direction='in' type='b'/>"
        "    </method>"
        "    <method name='CanSuspend'>"
        "      <arg name='result' direction='out' type='s'/>"
        "    </method>"
        "    <method name='Suspend'>"
        "      <arg name='interactive' direction='in' type='b'/>"
        "    </method>"
        "    <method name='CanHibernate'>"
        "      <arg name='result' direction='out' type='s'/>"
        "    </method>"
        "    <method name='Hibernate'>"
        "      <arg name='interactive' direction='in' type='b'/>"
        "    </method>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable login1_vtable =
    {
        handle_login1_call,
    };
    GDBusNodeInfo *login1_info;
    GError *error = NULL;

    login1_info = g_dbus_node_info_new_for_xml (login1_interface, &error);
    if (error)
        g_warning ("Failed to parse logind D-Bus interface: %s", error->message);
    g_clear_error (&error);
    if (!login1_info)
        return;
    g_dbus_connection_register_object (connection,
                                       "/org/freedesktop/login1",
                                       login1_info->interfaces[0],
                                       &login1_vtable,
                                       NULL, NULL,
                                       &error);
    if (error)
        g_warning ("Failed to register login1 service: %s", error->message);
    g_clear_error (&error);
    g_dbus_node_info_unref (login1_info);

    service_count--;
    if (service_count == 0)
        run_lightdm ();
}

static void
start_login1_daemon ()
{
    service_count++;
    g_bus_own_name (G_BUS_TYPE_SYSTEM,
                    "org.freedesktop.login1",
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    login1_name_acquired_cb,
                    NULL,
                    NULL,
                    NULL,
                    NULL);
}

static void
load_passwd_file ()
{
    gchar *path, *data, **lines;
    int i;

    path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "etc", "passwd", NULL);
    g_file_get_contents (path, &data, NULL, NULL);
    g_free (path);
    lines = g_strsplit (data, "\n", -1);
    g_free (data);

    for (i = 0; lines[i]; i++)
    {
        gchar **fields;
        guint uid;
        gchar *user_name, *real_name;
        GList *link;
        AccountsUser *user = NULL;
        GError *error = NULL;

        fields = g_strsplit (lines[i], ":", -1);
        if (fields == NULL || g_strv_length (fields) < 7)
            continue;

        user_name = fields[0];
        uid = atoi (fields[2]);
        real_name = fields[4];

        for (link = accounts_users; link; link = link->next)
        {
            AccountsUser *u = link->data;
            if (u->uid == uid)
            {
                user = u;
                break;
            }
        }
        if (!user)
        {
            gchar *path;
            GKeyFile *dmrc_file;

            user = g_malloc0 (sizeof (AccountsUser));
            accounts_users = g_list_append (accounts_users, user);

            dmrc_file = g_key_file_new ();
            path = g_build_filename (temp_dir, "home", user_name, ".dmrc", NULL);
            g_key_file_load_from_file (dmrc_file, path, G_KEY_FILE_NONE, NULL);
            g_free (path);

            user->uid = uid;
            user->user_name = g_strdup (user_name);
            user->real_name = g_strdup (real_name);
            user->home_directory = g_build_filename (temp_dir, "home", user_name, NULL);
            user->language = g_key_file_get_string (dmrc_file, "Desktop", "Language", NULL);
            /* DMRC contains a locale, strip the codeset off it to get the language */
            if (user->language)
            {
                gchar *c = strchr (user->language, '.');
                if (c)
                    *c = '\0';
            }
            user->xsession = g_key_file_get_string (dmrc_file, "Desktop", "Session", NULL);
            user->layouts = g_key_file_get_string_list (dmrc_file, "X-Accounts", "Layouts", NULL, NULL);
            user->path = g_strdup_printf ("/org/freedesktop/Accounts/User%d", uid);
            user->id = g_dbus_connection_register_object (accounts_connection,
                                                          user->path,
                                                          user_info->interfaces[0],
                                                          &user_vtable,
                                                          user,
                                                          NULL,
                                                          &error);
            if (error)
                g_warning ("Failed to register user: %s", error->message);
            g_clear_error (&error);

            g_key_file_free (dmrc_file);
        }

        g_strfreev (fields);
    }

    g_strfreev (lines);
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
    if (strcmp (method_name, "ListCachedUsers") == 0)
    {
        GVariantBuilder builder;
        GList *link;

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("ao"));

        load_passwd_file ();      
        for (link = accounts_users; link; link = link->next)
        {
            AccountsUser *user = link->data;
            g_variant_builder_add_value (&builder, g_variant_new_object_path (user->path));
        }

        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(ao)", &builder));
    }
    else if (strcmp (method_name, "FindUserByName") == 0)
    {
        GList *link;
        AccountsUser *user = NULL;
        gchar *user_name;

        g_variant_get (parameters, "(&s)", &user_name);

        load_passwd_file ();
        for (link = accounts_users; link; link = link->next)
        {
            AccountsUser *u = link->data;
            if (strcmp (u->user_name, user_name) == 0)
            {
                user = u;
                break;
            }
        }
        if (user)
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", user->path));
        else
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such user: %s", user_name);
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

    if (strcmp (property_name, "UserName") == 0)
        return g_variant_new_string (user->user_name);
    else if (strcmp (property_name, "RealName") == 0)
        return g_variant_new_string (user->real_name);
    else if (strcmp (property_name, "HomeDirectory") == 0)
        return g_variant_new_string (user->home_directory);
    else if (strcmp (property_name, "BackgroundFile") == 0)
        return g_variant_new_string ("");
    else if (strcmp (property_name, "Language") == 0)
        return g_variant_new_string (user->language ? user->language : "");
    else if (strcmp (property_name, "XSession") == 0)
        return g_variant_new_string (user->xsession ? user->xsession : "");
    else if (strcmp (property_name, "XKeyboardLayouts") == 0)
    {
        if (user->layouts != NULL)
            return g_variant_new_strv ((const gchar * const *) user->layouts, -1);
        else
            return g_variant_new_strv (NULL, 0);
    }
    else if (strcmp (property_name, "XHasMessages") == 0)
        return g_variant_new_boolean (FALSE);

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
        "    <method name='ListCachedUsers'>"
        "      <arg name='user' direction='out' type='ao'/>"
        "    </method>"
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
        "    <property name='UserName' type='s' access='read'/>"
        "    <property name='RealName' type='s' access='read'/>"
        "    <property name='HomeDirectory' type='s' access='read'/>"
        "    <property name='BackgroundFile' type='s' access='read'/>"
        "    <property name='Language' type='s' access='read'/>"
        "    <property name='XSession' type='s' access='read'/>"
        "    <property name='XKeyboardLayouts' type='as' access='read'/>"
        "    <property name='XHasMessages' type='b' access='read'/>"
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

    command_line = g_string_new ("lightdm");
    if (getenv ("DEBUG"))
        g_string_append (command_line, " --debug");
    g_string_append_printf (command_line, " --cache-dir %s/cache", temp_dir);
    g_string_append_printf (command_line, " --xsessions-dir=%s/usr/share/xsessions", temp_dir);
    g_string_append_printf (command_line, " --remote-sessions-dir=%s/usr/share/remote-sessions", temp_dir);
    g_string_append_printf (command_line, " --xgreeters-dir=%s/usr/share/xgreeters", temp_dir);

    test_runner_command = g_strdup_printf ("PATH=%s LD_PRELOAD=%s LD_LIBRARY_PATH=%s LIGHTDM_TEST_ROOT=%s DBUS_SESSION_BUS_ADDRESS=%s %s\n",
                                           g_getenv ("PATH"), g_getenv ("LD_PRELOAD"), g_getenv ("LD_LIBRARY_PATH"), g_getenv ("LIGHTDM_TEST_ROOT"), g_getenv ("DBUS_SESSION_BUS_ADDRESS"),
                                           command_line->str);

    if (!g_shell_parse_argv (command_line->str, NULL, &lightdm_argv, &error))
    {
        g_warning ("Error parsing command line: %s", error->message);
        quit (EXIT_FAILURE);
    }
    g_clear_error (&error);

    if (!g_spawn_async (NULL, lightdm_argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, NULL, NULL, &lightdm_pid, &error))
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
    GString *passwd_data, *group_data;
    GSource *status_source;
    gchar cwd[1024];
    GError *error = NULL;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

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
    path = g_strdup_printf ("%s/tests/src/.libs:%s/tests/src:%s/tests/src:%s/src:%s", BUILDDIR, BUILDDIR, SRCDIR, BUILDDIR, g_getenv ("PATH"));
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
    path1 = g_build_filename (BUILDDIR, "liblightdm-gobject", NULL);
    g_setenv ("GI_TYPELIB_PATH", path1, TRUE);
    g_free (path1);

    /* Run from a temporary directory */
    temp_dir = g_build_filename (g_get_tmp_dir (), "lightdm-test-XXXXXX", NULL);
    if (!mkdtemp (temp_dir))
    {
        g_warning ("Error creating temporary directory: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    g_chmod (temp_dir, 0755);
    g_setenv ("LIGHTDM_TEST_ROOT", temp_dir, TRUE);

    /* Open socket for status */
    status_socket_name = g_build_filename (temp_dir, ".status-socket", NULL);
    unlink (status_socket_name);
    status_socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
    if (error)
        g_warning ("Error creating status socket: %s", error->message);
    g_clear_error (&error);
    if (status_socket)
    {
        GSocketAddress *address;
        gboolean result;

        address = g_unix_socket_address_new (status_socket_name);
        result = g_socket_bind (status_socket, address, FALSE, &error);
        g_object_unref (address);
        if (error)
            g_warning ("Error binding status socket: %s", error->message);
        g_clear_error (&error);
        if (result)
        {
            result = g_socket_listen (status_socket, &error);
            if (error)
                g_warning ("Error listening on status socket: %s", error->message);
            g_clear_error (&error);
        }
        if (!result)
        {
            g_object_unref (status_socket);
            status_socket = NULL;
        }
    }
    if (!status_socket)
        quit (EXIT_FAILURE);
    status_source = g_socket_create_source (status_socket, G_IO_IN, NULL);
    g_source_set_callback (status_source, status_connect_cb, NULL, NULL);
    g_source_attach (status_source, NULL);

    /* Set up a skeleton file system */
    g_mkdir_with_parents (g_strdup_printf ("%s/etc", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/usr/share", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/tmp", temp_dir), 0755);

    /* Copy over the configuration */
    g_mkdir_with_parents (g_strdup_printf ("%s/etc/lightdm", temp_dir), 0755);
    if (!g_key_file_has_key (config, "test-runner-config", "have-config", NULL) || g_key_file_get_boolean (config, "test-runner-config", "have-config", NULL))
        if (system (g_strdup_printf ("cp %s %s/etc/lightdm/lightdm.conf", config_path, temp_dir)))
            perror ("Failed to copy configuration");

    /* Always copy the script */
    if (system (g_strdup_printf ("cp %s %s/script", config_path, temp_dir)))
        perror ("Failed to copy configuration");  

    /* Copy over the greeter files */
    if (system (g_strdup_printf ("cp -r %s/xsessions %s/usr/share", DATADIR, temp_dir)))
        perror ("Failed to copy xsessions");
    if (system (g_strdup_printf ("cp -r %s/remote-sessions %s/usr/share", DATADIR, temp_dir)))
        perror ("Failed to copy remote sessions");
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

    /* Make fake users */
    struct
    {
        gchar *user_name;
        gchar *password;
        gboolean have_home_dir;
        gchar *real_name;
        gchar *xsession;
        gchar *dmrc_layout;
        gchar *dbus_layouts;
        gchar *language;
        gint uid;
    } users[] =
    {
        /* Root account */
        {"root",             "",         TRUE,  "root",               NULL,  NULL, NULL,          NULL,             0},
        /* Unprivileged account for greeters */
        {"lightdm",          "",         TRUE,  "",                   NULL,  NULL, NULL,          NULL,           100},
        /* These accounts have a password */
        {"have-password1",   "password", TRUE,  "Password User 1",    NULL,  NULL, NULL,          NULL,          1000},
        {"have-password2",   "password", TRUE,  "Password User 2",    NULL,  NULL, NULL,          NULL,          1001},
        {"have-password3",   "password", TRUE,  "Password User 3",    NULL,  NULL, NULL,          NULL,          1002},
        {"have-password4",   "password", TRUE,  "Password User 4",    NULL,  NULL, NULL,          NULL,          1003},
        /* This account always prompts for a password, even if using the lightdm-autologin service */
        {"always-password",  "password", TRUE,  "Password User 4",    NULL,  NULL, NULL,          NULL,          1004},
        /* These accounts have no password */
        {"no-password1",     "",         TRUE,  "No Password User 1", NULL,  NULL, NULL,          NULL,          1005},
        {"no-password2",     "",         TRUE,  "No Password User 2", NULL,  NULL, NULL,          NULL,          1006},
        {"no-password3",     "",         TRUE,  "No Password User 3", NULL,  NULL, NULL,          NULL,          1007},
        {"no-password4",     "",         TRUE,  "No Password User 4", NULL,  NULL, NULL,          NULL,          1008},
        /* This account has a keyboard layout */
        {"have-layout",      "",         TRUE,  "Layout User",        NULL,  "us", NULL,          NULL,          1009},
        /* This account has a set of keyboard layouts */
        {"have-layouts",     "",         TRUE,  "Layouts User",       NULL,  "ru", "fr\toss;ru;", NULL,          1010},
        /* This account has a language set */
        {"have-language",    "",         TRUE,  "Language User",      NULL,  NULL, NULL,          "en_AU.utf8",  1011},      
        /* This account has a preconfigured session */
        {"have-session",            "",  TRUE,  "Session User", "alternative", NULL, NULL,        NULL,          1012},
        /* This account has the home directory mounted on login */
        {"mount-home-dir",   "",         FALSE, "Mounted Home Dir User", NULL, NULL, NULL,        NULL,          1013},
        /* This account is denied access */
        {"denied",           "",         TRUE,  "Denied User",        NULL,  NULL, NULL,          NULL,          1014},
        /* This account has expired */
        {"expired",          "",         TRUE,  "Expired User",       NULL,  NULL, NULL,          NULL,          1015},
        /* This account needs a password change */
        {"new-authtok",      "",         TRUE,  "New Token User",     NULL,  NULL, NULL,          NULL,          1016},
        /* This account is switched to change-user2 when authentication succeeds */
        {"change-user1",     "",         TRUE,  "Change User 1",      NULL,  NULL, NULL,          NULL,          1017},
        {"change-user2",     "",         TRUE,  "Change User 2",      NULL,  NULL, NULL,          NULL,          1018},
        /* This account switches to invalid-user when authentication succeeds */
        {"change-user-invalid", "",      TRUE,  "Invalid Change User",NULL,  NULL, NULL,          NULL,          1019},
        /* This account crashes on authentication */
        {"crash-authenticate", "",       TRUE,  "Crash Auth User",    NULL,  NULL, NULL,          NULL,          1020},
        /* This account shows an informational prompt on login */
        {"info-prompt",      "password", TRUE,  "Info Prompt",        NULL,  NULL, NULL,          NULL,          1021},
        /* This account shows multiple informational prompts on login */
        {"multi-info-prompt","password", TRUE,  "Multi Info Prompt",  NULL,  NULL, NULL,          NULL,          1022},
        /* This account uses two factor authentication */
        {"two-factor",       "password", TRUE,  "Two Factor",         NULL,  NULL, NULL,          NULL,          1023},
        /* This account has a special group */
        {"group-member",     "password", TRUE,  "Group Member",       NULL,  NULL, NULL,          NULL,          1024},
        /* This account has the home directory created when the session starts */
        {"make-home-dir",    "",         FALSE, "Make Home Dir User", NULL,  NULL, NULL,          NULL,          1025},
        /* This account fails to open a session */
        {"session-error",    "password", TRUE,  "Session Error",      NULL,  NULL, NULL,          NULL,          1026},
        /* This account can't establish credentials */
        {"cred-error",       "password", TRUE,  "Cred Error",         NULL,  NULL, NULL,          NULL,          1027},
        /* This account has expired credentials */
        {"cred-expired",     "password", TRUE,  "Cred Expired",       NULL,  NULL, NULL,          NULL,          1028},
        /* This account has cannot access their credentials */
        {"cred-unavail",     "password", TRUE,  "Cred Unavail",       NULL,  NULL, NULL,          NULL,          1029},
        /* This account sends informational messages for each PAM function that is called */
        {"log-pam",          "password", TRUE,  "Log PAM",            NULL,  NULL, NULL,          NULL,          1030},
        /* This account shows multiple prompts on login */
        {"multi-prompt",     "password", TRUE,  "Multi Prompt",       NULL,  NULL, NULL,          NULL,          1031},
        {NULL,               NULL,       FALSE, NULL,                 NULL,  NULL, NULL,          NULL,             0}
    };
    passwd_data = g_string_new ("");
    group_data = g_string_new ("");
    int i;
    for (i = 0; users[i].user_name; i++)
    {
        GKeyFile *dmrc_file;
        gboolean save_dmrc = FALSE;

        if (users[i].have_home_dir)
        {
            path = g_build_filename (home_dir, users[i].user_name, NULL);
            g_mkdir_with_parents (path, 0755);
            if (chown (path, users[i].uid, users[i].uid) < 0)
              g_debug ("chown (%s) failed: %s", path, strerror (errno));
            g_free (path);
        }

        dmrc_file = g_key_file_new ();
        if (users[i].xsession)
        {
            g_key_file_set_string (dmrc_file, "Desktop", "Session", users[i].xsession);
            save_dmrc = TRUE;
        }
        if (users[i].dmrc_layout)
        {
            g_key_file_set_string (dmrc_file, "Desktop", "Layout", users[i].dmrc_layout);
            save_dmrc = TRUE;
        }
        if (users[i].dbus_layouts)
        {
            g_key_file_set_string (dmrc_file, "X-Accounts", "Layouts", users[i].dbus_layouts);
            save_dmrc = TRUE;
        }
        if (users[i].language)
        {
            g_key_file_set_string (dmrc_file, "Desktop", "Language", users[i].language);
            save_dmrc = TRUE;
        }

        if (save_dmrc)
        {
            gchar *data;

            path = g_build_filename (home_dir, users[i].user_name, ".dmrc", NULL);
            data = g_key_file_to_data (dmrc_file, NULL, NULL);
            g_file_set_contents (path, data, -1, NULL);
            g_free (data);
            g_free (path);         
        }

        g_key_file_free (dmrc_file);

        /* Add passwd file entry */
        g_string_append_printf (passwd_data, "%s:%s:%d:%d:%s:%s/home/%s:/bin/sh\n", users[i].user_name, users[i].password, users[i].uid, users[i].uid, users[i].real_name, temp_dir, users[i].user_name);

        /* Add group file entry */
        g_string_append_printf (group_data, "%s:x:%d:%s\n", users[i].user_name, users[i].uid, users[i].user_name);
    }
    path = g_build_filename (temp_dir, "etc", "passwd", NULL);
    g_file_set_contents (path, passwd_data->str, -1, NULL);
    g_free (path);
    g_string_free (passwd_data, TRUE);

    /* Add an extra test group */
    g_string_append_printf (group_data, "test-group:x:111:\n");

    path = g_build_filename (temp_dir, "etc", "group", NULL);
    g_file_set_contents (path, group_data->str, -1, NULL);
    g_free (path);
    g_string_free (group_data, TRUE);

    /* Start D-Bus services */
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-upower", NULL))
        start_upower_daemon ();
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-console-kit", NULL))
        start_console_kit_daemon ();
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-login1", NULL))
        start_login1_daemon ();
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-accounts-service", NULL))
        start_accounts_service_daemon ();

    g_main_loop_run (loop);

    return EXIT_FAILURE;
}
