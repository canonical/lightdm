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
static int status_timeout_ms = 4000;

/* Timeout in ms to wait for SIGTERM to be handled by a child process */
#define KILL_TIMEOUT 2000

static gchar *test_runner_command;
static gchar *config_path;
static GKeyFile *config;
static GSocket *status_socket = NULL;
static gchar *status_socket_name = NULL;
static GList *statuses = NULL;
typedef struct
{
    gchar *text;
    gboolean done;
} ScriptLine;
static GList *script = NULL;
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
    gchar *image;
    gchar *background;
    gchar *path;
    guint id;
    gchar *language;
    gchar *xsession;
    gchar **layouts;
    gboolean has_messages;
    gboolean hidden;
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
static GDBusNodeInfo *ck_session_info;
typedef struct
{
    gchar *cookie;
    gchar *path;
    guint id;
    gboolean locked;
} CKSession;
static GList *ck_sessions = NULL;
static gint ck_session_index = 0;
static void handle_ck_session_call (GDBusConnection       *connection,
                                    const gchar           *sender,
                                    const gchar           *object_path,
                                    const gchar           *interface_name,
                                    const gchar           *method_name,
                                    GVariant              *parameters,
                                    GDBusMethodInvocation *invocation,
                                    gpointer               user_data);
static const GDBusInterfaceVTable ck_session_vtable =
{
    handle_ck_session_call,
};

typedef struct
{
    gchar *id;
    gchar *path;
    gboolean can_graphical;
    gboolean can_multi_session;
} Login1Seat;

static GList *login1_seats = NULL;

static Login1Seat *add_login1_seat (GDBusConnection *connection, const gchar *id, gboolean emit_signal);
static Login1Seat *find_login1_seat (const gchar *id);
static void remove_login1_seat (GDBusConnection *connection, const gchar *id);

typedef struct
{
    gchar *id;
    gchar *path;
    guint pid;
    gboolean locked;
} Login1Session;

static GList *login1_sessions = NULL;
static gint login1_session_index = 0;

typedef struct
{
    GSocket *socket;
    GSource *source;
} StatusClient;
static GList *status_clients = NULL;

static void ready (void);
static void quit (int status);
static void check_status (const gchar *status);
static AccountsUser *get_accounts_user_by_uid (guint uid);
static AccountsUser *get_accounts_user_by_name (const gchar *username);
static void accounts_user_set_hidden (AccountsUser *user, gboolean hidden, gboolean emit_signal);

static gboolean
kill_timeout_cb (gpointer data)
{
    Process *process = data;

    process->kill_timeout = 0;

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

    if (temp_dir && getenv ("DEBUG") == NULL)
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

static gchar *
get_prefix (const gchar *text)
{
    gchar *prefix;
    gint i;

    prefix = g_strdup (text);
    for (i = 0; prefix[i] != '\0' && prefix[i] != ' '; i++);
    prefix[i] = '\0';

    return prefix;
}

static ScriptLine *
get_script_line (const gchar *prefix)
{
    GList *link;

    for (link = script; link; link = link->next)
    {
        ScriptLine *line = link->data;

        /* Ignore lines with other prefixes */
        if (prefix)
        {
            gchar *p;
            gboolean matches;

            p = get_prefix (line->text);
            matches = strcmp (prefix, p) == 0;
            g_free (p);

            if (!matches)
                continue;
        }

        if (!line->done)
            return line;
    }

    return NULL;
}

static gboolean
stop_loop (gpointer user_data)
{
    g_main_loop_quit ((GMainLoop *)user_data);
    return G_SOURCE_REMOVE;
}

static void
switch_to_greeter_done_cb (GObject *bus, GAsyncResult *result, gpointer data)
{
    GVariant *r;
    GError *error = NULL;

    r = g_dbus_connection_call_finish (G_DBUS_CONNECTION (bus), result, &error);
    if (error)
        g_warning ("Failed to switch to greeter: %s\n", error->message);
    g_clear_error (&error);

    if (r)
    {
        check_status ("RUNNER SWITCH-TO-GREETER");
        g_variant_unref (r);
    }
    else
        check_status ("RUNNER SWITCH-TO-GREETER FAILED");
}

static void
switch_to_user_done_cb (GObject *bus, GAsyncResult *result, gpointer data)
{
    GVariant *r;
    GError *error = NULL;
    gchar *username = data, *status_text;

    r = g_dbus_connection_call_finish (G_DBUS_CONNECTION (bus), result, &error);
    if (error)
        g_warning ("Failed to switch to user: %s\n", error->message);
    g_clear_error (&error);

    if (r)
    {
        status_text = g_strdup_printf ("RUNNER SWITCH-TO-USER USERNAME=%s", username);
        g_variant_unref (r);
    }
    else
        status_text = g_strdup_printf ("RUNNER SWITCH-TO-USER USERNAME=%s FAILED", username);
    check_status (status_text);

    g_free (status_text);
    g_free (username);
}

static void
switch_to_guest_done_cb (GObject *bus, GAsyncResult *result, gpointer data)
{
    GVariant *r;
    GError *error = NULL;

    r = g_dbus_connection_call_finish (G_DBUS_CONNECTION (bus), result, &error);
    if (error)
        g_warning ("Failed to switch to guest: %s\n", error->message);
    g_clear_error (&error);

    if (r)
    {
        check_status ("RUNNER SWITCH-TO-GUEST");
        g_variant_unref (r);
    }
    else
        check_status ("RUNNER SWITCH-TO-GUEST FAILED");
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

    if (strcmp (name, "START-DAEMON") == 0)
    {
        GString *command_line;
        gchar **lightdm_argv;
        pid_t lightdm_pid;
        GError *error = NULL;

        command_line = g_string_new ("lightdm");
        if (getenv ("DEBUG"))
            g_string_append (command_line, " --debug");
        g_string_append_printf (command_line, " --cache-dir %s/cache", temp_dir);

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
    else if (strcmp (name, "WAIT") == 0)
    {
        /* Use a main loop so that our DBus functions are still responsive */
        GMainLoop *loop = g_main_loop_new (NULL, FALSE);
        g_timeout_add_seconds (1, stop_loop, loop);
        g_main_loop_run (loop);
        g_main_loop_unref (loop);
    }
    else if (strcmp (name, "ADD-SEAT") == 0)
    {
        const gchar *id, *v;
        Login1Seat *seat;

        id = g_hash_table_lookup (params, "ID");
        seat = add_login1_seat (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL), id, TRUE);
        v = g_hash_table_lookup (params, "CAN-GRAPHICAL");
        if (v)
            seat->can_graphical = strcmp (v, "TRUE") == 0;
        v = g_hash_table_lookup (params, "CAN-MULTI-SESSION");
        if (v)
            seat->can_multi_session = strcmp (v, "TRUE") == 0;
    }
    else if (strcmp (name, "UPDATE-SEAT") == 0)
    {
        Login1Seat *seat;
        const gchar *id;

        id = g_hash_table_lookup (params, "ID");
        seat = find_login1_seat (id);
        if (seat)
        {
            const gchar *v;
            GVariantBuilder invalidated_properties;
            GError *error = NULL;

            g_variant_builder_init (&invalidated_properties, G_VARIANT_TYPE_ARRAY);

            v = g_hash_table_lookup (params, "CAN-GRAPHICAL");
            if (v)
            {
                seat->can_graphical = strcmp (v, "TRUE") == 0;
                g_variant_builder_add (&invalidated_properties, "s", "CanGraphical");
            }
            v = g_hash_table_lookup (params, "CAN-MULTI-SESSION");
            if (v)
            {
                seat->can_multi_session = strcmp (v, "TRUE") == 0;
                g_variant_builder_add (&invalidated_properties, "s", "CanMultiSession");
            }

            g_dbus_connection_emit_signal (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                           NULL,
                                           seat->path,
                                           "org.freedesktop.DBus.Properties",
                                           "PropertiesChanged",
                                           g_variant_new ("(sa{sv}as)", "org.freedesktop.login1.Seat", NULL, &invalidated_properties),
                                           &error);
            if (error)
                g_warning ("Failed to emit PropertiesChanged: %s", error->message);
            g_clear_error (&error);
        }
    }
    else if (strcmp (name, "REMOVE-SEAT") == 0)
    {
        const gchar *id;
        id = g_hash_table_lookup (params, "ID");
        remove_login1_seat (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL), id);
    }
    else if (strcmp (name, "LIST-SEATS") == 0)
    {
        GVariant *result, *value;
        GString *status;
        GVariantIter *iter;
        const gchar *path;
        int i = 0;

        result = g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                              "org.freedesktop.DisplayManager",
                                              "/org/freedesktop/DisplayManager",
                                              "org.freedesktop.DBus.Properties",
                                              "Get",
                                              g_variant_new ("(ss)", "org.freedesktop.DisplayManager", "Seats"),
                                              G_VARIANT_TYPE ("(v)"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              G_MAXINT,
                                              NULL,
                                              NULL);

        status = g_string_new ("RUNNER LIST-SEATS SEATS=");
        g_variant_get (result, "(v)", &value);
        g_variant_get (value, "ao", &iter);
        while (g_variant_iter_loop (iter, "&o", &path))
        {
            if (i != 0)
                g_string_append (status, ",");
            g_string_append (status, path);
            i++;
        }
        g_variant_unref (value);
        g_variant_unref (result);

        check_status (status->str);
        g_string_free (status, TRUE);
    }
    else if (strcmp (name, "LIST-SESSIONS") == 0)
    {
        GVariant *result, *value;
        GString *status;
        GVariantIter *iter;
        const gchar *path;
        int i = 0;

        result = g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                              "org.freedesktop.DisplayManager",
                                              "/org/freedesktop/DisplayManager",
                                              "org.freedesktop.DBus.Properties",
                                              "Get",
                                              g_variant_new ("(ss)", "org.freedesktop.DisplayManager", "Sessions"),
                                              G_VARIANT_TYPE ("(v)"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              G_MAXINT,
                                              NULL,
                                              NULL);

        status = g_string_new ("RUNNER LIST-SESSIONS SESSIONS=");
        g_variant_get (result, "(v)", &value);
        g_variant_get (value, "ao", &iter);
        while (g_variant_iter_loop (iter, "&o", &path))
        {
            if (i != 0)
                g_string_append (status, ",");
            g_string_append (status, path);
            i++;
        }
        g_variant_unref (value);
        g_variant_unref (result);

        check_status (status->str);
        g_string_free (status, TRUE);
    }
    else if (strcmp (name, "SEAT-CAN-SWITCH") == 0)
    {
        GVariant *result, *value;
        gchar *status;

        result = g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                              "org.freedesktop.DisplayManager",
                                              "/org/freedesktop/DisplayManager/Seat0",
                                              "org.freedesktop.DBus.Properties",
                                              "Get",
                                              g_variant_new ("(ss)", "org.freedesktop.DisplayManager.Seat", "CanSwitch"),
                                              G_VARIANT_TYPE ("(v)"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              G_MAXINT,
                                              NULL,
                                              NULL);

        g_variant_get (result, "(v)", &value);
        status = g_strdup_printf ("RUNNER SEAT-CAN-SWITCH CAN-SWITCH=%s", g_variant_get_boolean (value) ? "TRUE" : "FALSE");
        g_variant_unref (value);
        g_variant_unref (result);
        check_status (status);
        g_free (status);
    }
    else if (strcmp (name, "SEAT-HAS-GUEST-ACCOUNT") == 0)
    {
        GVariant *result, *value;
        gchar *status;

        result = g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                              "org.freedesktop.DisplayManager",
                                              "/org/freedesktop/DisplayManager/Seat0",
                                              "org.freedesktop.DBus.Properties",
                                              "Get",
                                              g_variant_new ("(ss)", "org.freedesktop.DisplayManager.Seat", "HasGuestAccount"),
                                              G_VARIANT_TYPE ("(v)"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              G_MAXINT,
                                              NULL,
                                              NULL);

        g_variant_get (result, "(v)", &value);
        status = g_strdup_printf ("RUNNER SEAT-HAS-GUEST-ACCOUNT HAS-GUEST-ACCOUNT=%s", g_variant_get_boolean (value) ? "TRUE" : "FALSE");
        g_variant_unref (value);
        g_variant_unref (result);
        check_status (status);
        g_free (status);
    }
    else if (strcmp (name, "SWITCH-TO-GREETER") == 0)
    {
        g_dbus_connection_call (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                "org.freedesktop.DisplayManager",
                                "/org/freedesktop/DisplayManager/Seat0",
                                "org.freedesktop.DisplayManager.Seat",
                                "SwitchToGreeter",
                                g_variant_new ("()"),
                                G_VARIANT_TYPE ("()"),
                                G_DBUS_CALL_FLAGS_NONE,
                                G_MAXINT,
                                NULL,
                                switch_to_greeter_done_cb,
                                NULL);
    }
    else if (strcmp (name, "SWITCH-TO-USER") == 0)
    {
        const gchar *username;

        username = g_hash_table_lookup (params, "USERNAME");
        g_dbus_connection_call (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                "org.freedesktop.DisplayManager",
                                "/org/freedesktop/DisplayManager/Seat0",
                                "org.freedesktop.DisplayManager.Seat",
                                "SwitchToUser",
                                g_variant_new ("(ss)", username, ""),
                                G_VARIANT_TYPE ("()"),
                                G_DBUS_CALL_FLAGS_NONE,
                                G_MAXINT,
                                NULL,
                                switch_to_user_done_cb,
                                g_strdup (username));
    }
    else if (strcmp (name, "SWITCH-TO-GUEST") == 0)
    {
        g_dbus_connection_call (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                "org.freedesktop.DisplayManager",
                                "/org/freedesktop/DisplayManager/Seat0",
                                "org.freedesktop.DisplayManager.Seat",
                                "SwitchToGuest",
                                g_variant_new ("(s)", ""),
                                G_VARIANT_TYPE ("()"),
                                G_DBUS_CALL_FLAGS_NONE,
                                G_MAXINT,
                                NULL,
                                switch_to_guest_done_cb,
                                NULL);
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
    else if (strcmp (name, "ADD-USER") == 0)
    {
        gchar *status_text, *username;
        AccountsUser *user;

        username = g_hash_table_lookup (params, "USERNAME");
        user = get_accounts_user_by_name (username);
        if (user)
            accounts_user_set_hidden (user, FALSE, TRUE);
        else
            g_warning ("Unknown user %s", username);

        status_text = g_strdup_printf ("RUNNER ADD-USER USERNAME=%s", username);
        check_status (status_text);
        g_free (status_text);
    }
    else if (strcmp (name, "UPDATE-USER") == 0)
    {
        GString *status_text;
        gchar *username;
        AccountsUser *user;
        GError *error = NULL;

        status_text = g_string_new ("RUNNER UPDATE-USER USERNAME=");

        username = g_hash_table_lookup (params, "USERNAME");
        g_string_append (status_text, username);
        user = get_accounts_user_by_name (username);
        if (user)
        {
            if (g_hash_table_lookup (params, "NAME"))
            {
                user->user_name = g_strdup (g_hash_table_lookup (params, "NAME"));
                g_string_append_printf (status_text, " NAME=%s", user->user_name);
            }
            if (g_hash_table_lookup (params, "REAL-NAME"))
            {
                user->real_name = g_strdup (g_hash_table_lookup (params, "REAL-NAME"));
                g_string_append_printf (status_text, " REAL-NAME=%s", user->real_name);
            }
            if (g_hash_table_lookup (params, "HOME-DIRECTORY"))
            {
                user->home_directory = g_strdup (g_hash_table_lookup (params, "HOME-DIRECTORY"));
                g_string_append_printf (status_text, " HOME-DIRECTORY=%s", user->home_directory);
            }
            if (g_hash_table_lookup (params, "IMAGE"))
            {
                user->image = g_strdup (g_hash_table_lookup (params, "IMAGE"));
                g_string_append_printf (status_text, " IMAGE=%s", user->image);
            }
            if (g_hash_table_lookup (params, "BACKGROUND"))
            {
                user->background = g_strdup (g_hash_table_lookup (params, "BACKGROUND"));
                g_string_append_printf (status_text, " BACKGROUND=%s", user->background);
            }
            if (g_hash_table_lookup (params, "LANGUAGE"))
            {
                user->language = g_strdup (g_hash_table_lookup (params, "LANGUAGE"));
                g_string_append_printf (status_text, " LANGUAGE=%s", user->language);
            }
            if (g_hash_table_lookup (params, "LAYOUTS"))
            {
                const gchar *value = g_hash_table_lookup (params, "LAYOUTS");
                user->layouts = g_strsplit (value, ";", -1);
                g_string_append_printf (status_text, " LAYOUTS=%s", value);
            }
            if (g_hash_table_lookup (params, "HAS-MESSAGES"))
            {
                user->has_messages = g_strcmp0 (g_hash_table_lookup (params, "HAS-MESSAGES"), "TRUE") == 0;
                g_string_append_printf (status_text, " HAS-MESSAGES=%s", user->has_messages ? "TRUE" : "FALSE");
            }
            if (g_hash_table_lookup (params, "SESSION"))
            {
                user->xsession = g_strdup (g_hash_table_lookup (params, "SESSION"));
                g_string_append_printf (status_text, " SESSION=%s", user->xsession);
            }
        }
        else
            g_warning ("Unknown user %s", username);

        g_dbus_connection_emit_signal (accounts_connection,
                                       NULL,
                                       user->path,
                                       "org.freedesktop.Accounts.User",
                                       "Changed",
                                       g_variant_new ("()"),
                                       &error);
        if (error)
            g_warning ("Failed to emit Changed: %s", error->message);
        g_clear_error (&error);

        check_status (status_text->str);
        g_string_free (status_text, TRUE);
    }
    else if (strcmp (name, "DELETE-USER") == 0)
    {
        gchar *status_text, *username;
        AccountsUser *user;

        username = g_hash_table_lookup (params, "USERNAME");
        user = get_accounts_user_by_name (username);
        if (user)
            accounts_user_set_hidden (user, TRUE, TRUE);
        else
            g_warning ("Unknown user %s", username);

        status_text = g_strdup_printf ("RUNNER DELETE-USER USERNAME=%s", username);
        check_status (status_text);
        g_free (status_text);
    }
    /* Forward to external processes */
    else if (g_str_has_prefix (name, "SESSION-") ||
             g_str_has_prefix (name, "GREETER-") ||
             g_str_has_prefix (name, "XSERVER-") ||
             g_str_has_prefix (name, "XMIR-") ||
             strcmp (name, "UNITY-SYSTEM-COMPOSITOR") == 0)
    {
        GList *link;
        for (link = status_clients; link; link = link->next)
        {
            StatusClient *client = link->data;
            int length;
            GError *error = NULL;

            length = strlen (command);
            if (g_socket_send (client->socket, (gchar *) &length, sizeof (length), NULL, &error) < 0 ||
                g_socket_send (client->socket, command, strlen (command), NULL, &error) < 0)
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
run_commands (void)
{
    /* Stop daemon if requested */
    while (TRUE)
    {
        ScriptLine *line;

        /* Commands start with an asterisk */
        line = get_script_line (NULL);
        if (!line || line->text[0] != '*')
            break;

        statuses = g_list_append (statuses, g_strdup (line->text));
        line->done = TRUE;

        handle_command (line->text + 1);
    }

    /* Stop at the end of the script */
    if (get_script_line (NULL) == NULL)
        quit (EXIT_SUCCESS);
}

static gboolean
status_timeout_cb (gpointer data)
{
    ScriptLine *line;

    line = get_script_line (NULL);
    fail ("(timeout)", line ? line->text : NULL);

    return FALSE;
}

static void
check_status (const gchar *status)
{
    ScriptLine *line;
    gboolean result = FALSE;
    gchar *prefix;

    if (stop)
        return;

    statuses = g_list_append (statuses, g_strdup (status));

    if (getenv ("DEBUG"))
        g_print ("%s\n", status);

    /* Try and match against expected */
    prefix = get_prefix (status);
    line = get_script_line (prefix);
    g_free (prefix);
    if (line)
    {
        gchar *full_pattern = g_strdup_printf ("^%s$", line->text);
        result = g_regex_match_simple (full_pattern, status, 0, 0);
        g_free (full_pattern);
    }

    if (!result)
    {
        if (line == NULL)
            line = get_script_line (NULL);
        fail (NULL, line ? line->text : NULL);
        return;
    }

    line->done = TRUE;

    /* Restart timeout */
    if (status_timeout)
        g_source_remove (status_timeout);
    status_timeout = g_timeout_add (status_timeout_ms, status_timeout_cb, NULL);

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
        gchar *text = g_strstrip (lines[i]);
        if (g_str_has_prefix (text, "#?"))
        {
            ScriptLine *line;
            line = g_malloc0 (sizeof (ScriptLine));
            line->text = g_strdup (text + 2);
            line->done = FALSE;
            script = g_list_append (script, line);
        }
    }
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
        ready ();
}

static void
start_upower_daemon (void)
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
open_ck_session (GDBusConnection *connection, GVariant *params)
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
    session->id = g_dbus_connection_register_object (connection,
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
        CKSession *session = open_ck_session (connection, g_variant_builder_end (&params));
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", session->cookie));
    }
    else if (strcmp (method_name, "OpenSessionWithParameters") == 0)
    {
        CKSession *session = open_ck_session (connection, g_variant_get_child_value (parameters, 0));
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
            if (strcmp (session->cookie, cookie) == 0)
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

static void
handle_ck_session_call (GDBusConnection       *connection,
                        const gchar           *sender,
                        const gchar           *object_path,
                        const gchar           *interface_name,
                        const gchar           *method_name,
                        GVariant              *parameters,
                        GDBusMethodInvocation *invocation,
                        gpointer               user_data)
{
    CKSession *session = user_data;

    if (strcmp (method_name, "Lock") == 0)
    {
        if (!session->locked)
            check_status ("CONSOLE-KIT LOCK-SESSION");
        session->locked = TRUE;
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "Unlock") == 0)
    {
        if (session->locked)
            check_status ("CONSOLE-KIT UNLOCK-SESSION");
        session->locked = FALSE;
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "Activate") == 0)
    {
        gchar *status = g_strdup_printf ("CONSOLE-KIT ACTIVATE-SESSION SESSION=%s", session->cookie);
        check_status (status);
        g_free (status);

        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
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
        "    <method name='Activate'/>"
        "  </interface>"
        "</node>";
    GDBusNodeInfo *ck_info;
    GError *error = NULL;

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
        ready ();
}

static void
start_console_kit_daemon (void)
{
    service_count++;
    g_bus_own_name (G_BUS_TYPE_SYSTEM,
                    "org.freedesktop.ConsoleKit",
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    NULL,
                    ck_name_acquired_cb,
                    NULL,
                    NULL,
                    NULL);
}

static void
handle_login1_seat_call (GDBusConnection       *connection,
                         const gchar           *sender,
                         const gchar           *object_path,
                         const gchar           *interface_name,
                         const gchar           *method_name,
                         GVariant              *parameters,
                         GDBusMethodInvocation *invocation,
                         gpointer               user_data)
{
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such method: %s", method_name);
}

static GVariant *
handle_login1_seat_get_property (GDBusConnection       *connection,
                                 const gchar           *sender,
                                 const gchar           *object_path,
                                 const gchar           *interface_name,
                                 const gchar           *property_name,
                                 GError               **error,
                                 gpointer               user_data)
{
    Login1Seat *seat = user_data;

    if (strcmp (property_name, "CanGraphical") == 0)
        return g_variant_new_boolean (seat->can_graphical);
    else if (strcmp (property_name, "CanMultiSession") == 0)
        return g_variant_new_boolean (seat->can_multi_session);
    else if (strcmp (property_name, "Id") == 0)
        return g_variant_new_string (seat->id);
    else
        return NULL;
}

static Login1Seat *
add_login1_seat (GDBusConnection *connection, const gchar *id, gboolean emit_signal)
{
    Login1Seat *seat;
    GError *error = NULL;
    GDBusNodeInfo *login1_seat_info;

    const gchar *login1_seat_interface =
        "<node>"
        "  <interface name='org.freedesktop.login1.Seat'>"
        "    <property name='CanGraphical' type='b' access='read'/>"
        "    <property name='CanMultiSession' type='b' access='read'/>"
        "    <property name='Id' type='s' access='read'/>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable login1_seat_vtable =
    {
        handle_login1_seat_call,
        handle_login1_seat_get_property,
    };

    seat = g_malloc0 (sizeof (Login1Seat));
    login1_seats = g_list_append (login1_seats, seat);
    seat->id = g_strdup (id);
    seat->path = g_strdup_printf ("/org/freedesktop/login1/seat/%s", seat->id);
    seat->can_graphical = TRUE;
    seat->can_multi_session = TRUE;

    login1_seat_info = g_dbus_node_info_new_for_xml (login1_seat_interface, &error);
    if (error)
        g_warning ("Failed to parse login1 seat D-Bus interface: %s", error->message);
    g_clear_error (&error);
    if (!login1_seat_info)
        return NULL;

    g_dbus_connection_register_object (connection,
                                       seat->path,
                                       login1_seat_info->interfaces[0],
                                       &login1_seat_vtable,
                                       seat,
                                       NULL,
                                       &error);
    if (error)
        g_warning ("Failed to register login1 seat: %s", error->message);
    g_clear_error (&error);
    g_dbus_node_info_unref (login1_seat_info);

    if (emit_signal)
    {
        g_dbus_connection_emit_signal (connection,
                                       NULL,
                                       "/org/freedesktop/login1",
                                       "org.freedesktop.login1.Manager",
                                       "SeatNew",
                                       g_variant_new ("(so)", seat->id, seat->path),
                                       &error);
        if (error)
            g_warning ("Failed to emit SeatNew: %s", error->message);
        g_clear_error (&error);
    }

    return seat;
}

static Login1Seat *
find_login1_seat (const gchar *id)
{
    Login1Seat *seat;
    GList *link;

    for (link = login1_seats; link; link = link->next)
    {
        seat = link->data;
        if (strcmp (seat->id, id) == 0)
            return seat;
    }

    return NULL;
}

static void
remove_login1_seat (GDBusConnection *connection, const gchar *id)
{
    Login1Seat *seat;
    GError *error = NULL;

    seat = find_login1_seat (id);
    if (!seat)
        return;

    g_dbus_connection_emit_signal (connection,
                                   NULL,
                                   "/org/freedesktop/login1",
                                   "org.freedesktop.login1.Manager",
                                   "SeatRemoved",
                                   g_variant_new ("(so)", seat->id, seat->path),
                                   &error);
    if (error)
        g_warning ("Failed to emit SeatNew: %s", error->message);
    g_clear_error (&error);

    login1_seats = g_list_remove (login1_seats, seat);
    g_free (seat->id);
    g_free (seat->path);
    g_free (seat);
}

static void
handle_login1_session_call (GDBusConnection       *connection,
                            const gchar           *sender,
                            const gchar           *object_path,
                            const gchar           *interface_name,
                            const gchar           *method_name,
                            GVariant              *parameters,
                            GDBusMethodInvocation *invocation,
                            gpointer               user_data)
{
    /*Login1Session *session = user_data;*/
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such method: %s", method_name);
}

static Login1Session *
create_login1_session (GDBusConnection *connection)
{
    Login1Session *session;
    GError *error = NULL;
    GDBusNodeInfo *login1_session_info;

    const gchar *login1_session_interface =
        "<node>"
        "  <interface name='org.freedesktop.login1.Session'>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable login1_session_vtable =
    {
        handle_login1_session_call,
    };

    session = g_malloc0 (sizeof (Login1Session));
    login1_sessions = g_list_append (login1_sessions, session);

    session->id = g_strdup_printf ("c%d", login1_session_index++);
    session->path = g_strdup_printf ("/org/freedesktop/login1/Session/%s", session->id);

    login1_session_info = g_dbus_node_info_new_for_xml (login1_session_interface, &error);
    if (error)
        g_warning ("Failed to parse login1 session D-Bus interface: %s", error->message);
    g_clear_error (&error);
    if (!login1_session_info)
        return NULL;

    g_dbus_connection_register_object (connection,
                                       session->path,
                                       login1_session_info->interfaces[0],
                                       &login1_session_vtable,
                                       session,
                                       NULL,
                                       &error);
    if (error)
        g_warning ("Failed to register login1 session: %s", error->message);
    g_clear_error (&error);
    g_dbus_node_info_unref (login1_session_info);

    return session;
}

static Login1Session *
find_login1_session (const gchar *id)
{
    GList *link;

    for (link = login1_sessions; link; link = link->next)
    {
        Login1Session *session = link->data;
        if (strcmp (session->id, id) == 0)
            return session;
    }

    return NULL;
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
    if (strcmp (method_name, "ListSeats") == 0)
    {
        GVariantBuilder seats;
        GList *link;

        g_variant_builder_init (&seats, G_VARIANT_TYPE ("a(so)"));
        for (link = login1_seats; link; link = link->next)
        {
            Login1Seat *seat = link->data;
            g_variant_builder_add (&seats, "(so)", seat->id, seat->path);
        }
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(a(so))", &seats));
    }
    else if (strcmp (method_name, "CreateSession") == 0)
    {
        /* Note: this is not the full CreateSession as used by logind, we just
           need one so our fake PAM stack can communicate with this service */
        Login1Session *session = create_login1_session (connection);
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(so)", session->id, session->path));

    }
    else if (strcmp (method_name, "LockSession") == 0)
    {
        const gchar *id;
        Login1Session *session;

        g_variant_get (parameters, "(&s)", &id);
        session = find_login1_session (id);
        if (!session)
        {
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such session: %s", id);
            return;
        }

        if (!session->locked)
        {
            gchar *status = g_strdup_printf ("LOGIN1 LOCK-SESSION SESSION=%s", id);
            check_status (status);
            g_free (status);
        }
        session->locked = TRUE;
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "UnlockSession") == 0)
    {
        const gchar *id;
        Login1Session *session;

        g_variant_get (parameters, "(&s)", &id);
        session = find_login1_session (id);
        if (!session)
        {
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such session: %s", id);
            return;
        }

        if (session->locked)
        {
            gchar *status = g_strdup_printf ("LOGIN1 UNLOCK-SESSION SESSION=%s", id);
            check_status (status);
            g_free (status);
        }
        session->locked = FALSE;
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
    }
    else if (strcmp (method_name, "ActivateSession") == 0)
    {
        const gchar *id;
        Login1Session *session;

        g_variant_get (parameters, "(&s)", &id);
        session = find_login1_session (id);
        if (!session)
        {
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "No such session: %s", id);
            return;
        }

        gchar *status = g_strdup_printf ("LOGIN1 ACTIVATE-SESSION SESSION=%s", id);
        check_status (status);
        g_free (status);

        g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));
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
        "    <method name='ListSeats'>"
        "      <arg name='seats' type='a(so)' direction='out'/>"
        "    </method>"
        "    <method name='CreateSession'>"
        "      <arg name='id' type='s' direction='out'/>"
        "      <arg name='path' type='o' direction='out'/>"
        "    </method>"
        "    <method name='LockSession'>"
        "      <arg name='id' type='s' direction='in'/>"
        "    </method>"
        "    <method name='UnlockSession'>"
        "      <arg name='id' type='s' direction='in'/>"
        "    </method>"
        "    <method name='ActivateSession'>"
        "      <arg name='id' type='s' direction='in'/>"
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
        "    <signal name='SeatNew'>"
        "      <arg name='seat' type='so'/>"
        "    </signal>"
        "    <signal name='SeatRemoved'>"
        "      <arg name='seat' type='so'/>"
        "    </signal>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable login1_vtable =
    {
        handle_login1_call,
    };
    GDBusNodeInfo *login1_info;
    Login1Seat *seat0;
    GError *error = NULL;

    login1_info = g_dbus_node_info_new_for_xml (login1_interface, &error);
    if (error)
        g_warning ("Failed to parse login1 D-Bus interface: %s", error->message);
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

    /* We always have seat0 */
    seat0 = add_login1_seat (connection, "seat0", FALSE);
    if (g_key_file_has_key (config, "test-runner-config", "seat0-can-graphical", NULL))
        seat0->can_graphical = g_key_file_get_boolean (config, "test-runner-config", "seat0-can-graphical", NULL);
    if (g_key_file_has_key (config, "test-runner-config", "seat0-can-multi-session", NULL))
        seat0->can_multi_session = g_key_file_get_boolean (config, "test-runner-config", "seat0-can-multi-session", NULL);

    service_count--;
    if (service_count == 0)
        ready ();
}

static void
start_login1_daemon (void)
{
    service_count++;
    g_bus_own_name (G_BUS_TYPE_SYSTEM,
                    "org.freedesktop.login1",
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    NULL,
                    login1_name_acquired_cb,
                    NULL,
                    NULL,
                    NULL);
}

static AccountsUser *
get_accounts_user_by_uid (guint uid)
{
    GList *link;

    for (link = accounts_users; link; link = link->next)
    {
        AccountsUser *u = link->data;
        if (u->uid == uid)
            return u;
    }

    return NULL;
}

static AccountsUser *
get_accounts_user_by_name (const gchar *username)
{
    GList *link;

    for (link = accounts_users; link; link = link->next)
    {
        AccountsUser *u = link->data;
        if (strcmp (u->user_name, username) == 0)
            return u;
    }

    return NULL;
}

static void
accounts_user_set_hidden (AccountsUser *user, gboolean hidden, gboolean emit_signal)
{
    GError *error = NULL;

    user->hidden = hidden;

    if (user->hidden && user->id != 0)
    {
        g_dbus_connection_unregister_object (accounts_connection, user->id);
        g_dbus_connection_emit_signal (accounts_connection,
                                       NULL,
                                       "/org/freedesktop/Accounts",
                                       "org.freedesktop.Accounts",
                                       "UserDeleted",
                                       g_variant_new ("(o)", user->path),
                                       &error);
        if (error)
            g_warning ("Failed to emit UserDeleted: %s", error->message);
        g_clear_error (&error);

        user->id = 0;
    }
    if (!user->hidden && user->id == 0)
    {
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

        g_dbus_connection_emit_signal (accounts_connection,
                                       NULL,
                                       "/org/freedesktop/Accounts",
                                       "org.freedesktop.Accounts",
                                       "UserAdded",
                                       g_variant_new ("(o)", user->path),
                                       &error);
        if (error)
            g_warning ("Failed to emit UserAdded: %s", error->message);
        g_clear_error (&error);
    }
}

static void
load_passwd_file (void)
{
    gchar *path, *data, **lines;
    gchar **user_filter = NULL;
    int i;

    if (g_key_file_has_key (config, "test-runner-config", "accounts-service-user-filter", NULL))
    {
        gchar *filter;

        filter = g_key_file_get_string (config, "test-runner-config", "accounts-service-user-filter", NULL);
        user_filter = g_strsplit (filter, " ", -1);
        g_free (filter);
    }

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
        AccountsUser *user = NULL;

        fields = g_strsplit (lines[i], ":", -1);
        if (fields == NULL || g_strv_length (fields) < 7)
        {
            g_strfreev (fields);
            continue;
        }

        user_name = fields[0];
        uid = atoi (fields[2]);
        real_name = fields[4];

        user = get_accounts_user_by_uid (uid);
        if (!user)
        {
            gchar *path;
            GKeyFile *dmrc_file;

            user = g_malloc0 (sizeof (AccountsUser));
            accounts_users = g_list_append (accounts_users, user);

            /* Only allow users in whitelist */
            user->hidden = FALSE;
            if (user_filter)
            {
                int j;

                user->hidden = TRUE;
                for (j = 0; user_filter[j] != NULL; j++)
                    if (strcmp (user_name, user_filter[j]) == 0)
                        user->hidden = FALSE;
            }

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
            if (!user->layouts)
            {
                user->layouts = g_malloc (sizeof (gchar *) * 2);
                user->layouts[0] = g_key_file_get_string (dmrc_file, "Desktop", "Layout", NULL);
                user->layouts[1] = NULL;
            }
            user->has_messages = g_key_file_get_boolean (dmrc_file, "X-Accounts", "HasMessages", NULL);
            user->path = g_strdup_printf ("/org/freedesktop/Accounts/User%d", uid);
            accounts_user_set_hidden (user, user->hidden, FALSE);

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
            if (!user->hidden && user->uid >= 1000)
                g_variant_builder_add_value (&builder, g_variant_new_object_path (user->path));
        }

        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(ao)", &builder));
    }
    else if (strcmp (method_name, "FindUserByName") == 0)
    {
        AccountsUser *user = NULL;
        gchar *user_name;

        g_variant_get (parameters, "(&s)", &user_name);

        load_passwd_file ();
        user = get_accounts_user_by_name (user_name);
        if (user)
        {
            if (user->hidden)
                accounts_user_set_hidden (user, FALSE, TRUE);
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", user->path));
        }
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

        /* And notify others that it took */
        g_dbus_connection_emit_signal (accounts_connection,
                                       NULL,
                                       user->path,
                                       "org.freedesktop.Accounts.User",
                                       "Changed",
                                       g_variant_new ("()"),
                                       NULL);
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
    else if (strcmp (property_name, "SystemAccount") == 0)
        return g_variant_new_boolean (user->uid < 1000);
    else if (strcmp (property_name, "BackgroundFile") == 0)
        return g_variant_new_string (user->background ? user->background : "");
    else if (strcmp (property_name, "Language") == 0)
        return g_variant_new_string (user->language ? user->language : "");
    else if (strcmp (property_name, "IconFile") == 0)
        return g_variant_new_string (user->image ? user->image : "");
    else if (strcmp (property_name, "Shell") == 0)
        return g_variant_new_string ("/bin/sh");
    else if (strcmp (property_name, "Uid") == 0)
        return g_variant_new_uint64 (user->uid);
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
        return g_variant_new_boolean (user->has_messages);

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
        "    <signal name='UserAdded'>"
        "      <arg name='user' type='o'/>"
        "    </signal>"
        "    <signal name='UserDeleted'>"
        "      <arg name='user' type='o'/>"
        "    </signal>"
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
        "    <property name='SystemAccount' type='b' access='read'/>"
        "    <property name='BackgroundFile' type='s' access='read'/>"
        "    <property name='Language' type='s' access='read'/>"
        "    <property name='IconFile' type='s' access='read'/>"
        "    <property name='Shell' type='s' access='read'/>"
        "    <property name='Uid' type='t' access='read'/>"
        "    <property name='XSession' type='s' access='read'/>"
        "    <property name='XKeyboardLayouts' type='as' access='read'/>"
        "    <property name='XHasMessages' type='b' access='read'/>"
        "    <signal name='Changed' />"
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
        ready ();
}

static void
start_accounts_service_daemon (void)
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
ready (void)
{
    run_commands ();
}

static gboolean
signal_cb (gpointer user_data)
{
    g_print ("Caught signal, quitting\n");
    quit (EXIT_FAILURE);
    return FALSE;
}

static void
properties_changed_cb (GDBusConnection *connection,
                       const gchar *sender_name,
                       const gchar *object_path,
                       const gchar *interface_name,
                       const gchar *signal_name,
                       GVariant *parameters,
                       gpointer user_data)
{
    const gchar *interface, *name;
    GString *status;
    GVariant *value;
    GVariantIter *changed_properties, *invalidated_properties;
    int i;

    g_variant_get (parameters, "(&sa{sv}as)", &interface, &changed_properties, &invalidated_properties);

    status = g_string_new ("RUNNER DBUS-PROPERTIES-CHANGED");
    g_string_append_printf (status, " PATH=%s", object_path);
    g_string_append_printf (status, " INTERFACE=%s", interface);
    for (i = 0; g_variant_iter_loop (changed_properties, "{&sv}", &name, &value); i++)
    {
        if (i == 0)
            g_string_append (status, " CHANGED=");
        else
            g_string_append (status, ",");
        g_string_append (status, name);
        if (g_variant_is_of_type (value, G_VARIANT_TYPE ("ao")))
        {
            GVariantIter iter;
            const gchar *path;

            g_variant_iter_init (&iter, value);
            while (g_variant_iter_loop (&iter, "&o", &path))
                g_string_append_printf (status, ":%s", path);
        }
    }
    for (i = 0; g_variant_iter_loop (invalidated_properties, "&s", &name); i++)
    {
        if (i == 0)
            g_string_append (status, " INVALIDATED=");
        else
            g_string_append (status, ",");
        g_string_append (status, name);
    }

    check_status (status->str);
    g_string_free (status, TRUE);
}

static void
dbus_signal_cb (GDBusConnection *connection,
                const gchar *sender_name,
                const gchar *object_path,
                const gchar *interface_name,
                const gchar *signal_name,
                GVariant *parameters,
                gpointer user_data)
{
    GString *status;

    status = g_string_new ("RUNNER DBUS-SIGNAL");
    g_string_append_printf (status, " PATH=%s", object_path);
    g_string_append_printf (status, " INTERFACE=%s", interface_name);
    g_string_append_printf (status, " NAME=%s", signal_name);

    check_status (status->str);
    g_string_free (status, TRUE);
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    int i;
    gchar *greeter = NULL, *script_name, *config_file, *additional_system_config;
    gchar *additional_config, *path, *path1, *path2, *ld_preload, *ld_library_path, *home_dir;
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

    /* Don't let XDG vars from system affect tests */
    g_unsetenv ("XDG_CONFIG_DIRS");
    g_unsetenv ("XDG_DATA_DIRS");

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

    /* Run in a temporary directory inside the build directory */
    /* Note we have to pick a name that is short since Unix sockets in this directory have a 108 character limit on their paths */
    i = 0;
    while (TRUE) {
        gchar *name;

        name = g_strdup_printf (".r%d", i);
        g_free (temp_dir);
        temp_dir = g_build_filename ("/tmp", name, NULL);
        g_free (name);
        if (!g_file_test (temp_dir, G_FILE_TEST_EXISTS))
            break;
        i++;
    }
    g_mkdir_with_parents (temp_dir, 0755);
    g_setenv ("LIGHTDM_TEST_ROOT", temp_dir, TRUE);

    /* Open socket for status */
    /* Note we have to pick a socket name that is short since there is a 108 character limit on the name */
    status_socket_name = g_build_filename (temp_dir, ".s", NULL);
    unlink (status_socket_name);
    status_socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
    if (error)
        g_warning ("Error creating status socket %s: %s", status_socket_name, error->message);
    g_clear_error (&error);
    if (status_socket)
    {
        GSocketAddress *address;
        gboolean result;

        address = g_unix_socket_address_new (status_socket_name);
        result = g_socket_bind (status_socket, address, FALSE, &error);
        g_object_unref (address);
        if (error)
            g_warning ("Error binding status socket %s: %s", status_socket_name, error->message);
        g_clear_error (&error);
        if (result)
        {
            result = g_socket_listen (status_socket, &error);
            if (error)
                g_warning ("Error listening on status socket %s: %s", status_socket_name, error->message);
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
    g_mkdir_with_parents (g_strdup_printf ("%s/run", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/usr/share", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/usr/share/lightdm/sessions", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/usr/share/lightdm/remote-sessions", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/usr/share/lightdm/greeters", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/tmp", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/var/lib/lightdm-data", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/var/run", temp_dir), 0755);
    g_mkdir_with_parents (g_strdup_printf ("%s/var/log", temp_dir), 0755);

    /* Copy over the configuration */
    g_mkdir_with_parents (g_strdup_printf ("%s/etc/lightdm", temp_dir), 0755);
    if (!g_key_file_has_key (config, "test-runner-config", "have-config", NULL) || g_key_file_get_boolean (config, "test-runner-config", "have-config", NULL))
        if (system (g_strdup_printf ("cp %s %s/etc/lightdm/lightdm.conf", config_path, temp_dir)))
            perror ("Failed to copy configuration");

    additional_system_config = g_key_file_get_string (config, "test-runner-config", "additional-system-config", NULL);
    if (additional_system_config)
    {
        gchar **files;

        g_mkdir_with_parents (g_strdup_printf ("%s/usr/share/lightdm/lightdm.conf.d", temp_dir), 0755);

        files = g_strsplit (additional_system_config, " ", -1);
        for (i = 0; files[i]; i++)
            if (system (g_strdup_printf ("cp %s/tests/scripts/%s %s/usr/share/lightdm/lightdm.conf.d", SRCDIR, files[i], temp_dir)))
                perror ("Failed to copy configuration");
        g_strfreev (files);
    }

    additional_config = g_key_file_get_string (config, "test-runner-config", "additional-config", NULL);
    if (additional_config)
    {
        gchar **files;

        g_mkdir_with_parents (g_strdup_printf ("%s/etc/xdg/lightdm/lightdm.conf.d", temp_dir), 0755);

        files = g_strsplit (additional_config, " ", -1);
        for (i = 0; files[i]; i++)
            if (system (g_strdup_printf ("cp %s/tests/scripts/%s %s/etc/xdg/lightdm/lightdm.conf.d", SRCDIR, files[i], temp_dir)))
                perror ("Failed to copy configuration");
        g_strfreev (files);
    }

    if (g_key_file_has_key (config, "test-runner-config", "shared-data-dirs", NULL))
    {
        gchar *dir_string;
        gchar **dirs;
        gint i;

        dir_string = g_key_file_get_string (config, "test-runner-config", "shared-data-dirs", NULL);
        dirs = g_strsplit (dir_string, " ", -1);
        g_free (dir_string);

        for (i = 0; dirs[i]; i++)
        {
            gchar **fields = g_strsplit (dirs[i], ":", -1);
            if (g_strv_length (fields) == 4)
            {
                gchar *path = g_strdup_printf ("%s/var/lib/lightdm-data/%s", temp_dir, fields[0]);
                int uid = g_ascii_strtoll (fields[1], NULL, 10);
                int gid = g_ascii_strtoll (fields[2], NULL, 10);
                int mode = g_ascii_strtoll (fields[3], NULL, 8);
                g_mkdir (path, mode);
                g_chmod (path, mode); /* mkdir filters by umask, so make sure we have what we want */
                if (chown (path, uid, gid) < 0)
                  g_warning ("chown (%s) failed: %s", path, strerror (errno));
                g_free (path);
            }
            g_strfreev (fields);
        }

        g_strfreev (dirs);
    }

    /* Always copy the script */
    if (system (g_strdup_printf ("cp %s %s/script", config_path, temp_dir)))
        perror ("Failed to copy configuration");

    /* Copy over the greeter files */
    if (system (g_strdup_printf ("cp %s/sessions/* %s/usr/share/lightdm/sessions", DATADIR, temp_dir)))
        perror ("Failed to copy sessions");
    if (system (g_strdup_printf ("cp %s/remote-sessions/* %s/usr/share/lightdm/remote-sessions", DATADIR, temp_dir)))
        perror ("Failed to copy remote sessions");
    if (system (g_strdup_printf ("cp %s/greeters/* %s/usr/share/lightdm/greeters", DATADIR, temp_dir)))
        perror ("Failed to copy greeters");

    /* Set up the default greeter */
    path = g_build_filename (temp_dir, "usr", "share", "lightdm", "greeters", "default.desktop", NULL);
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
        gchar *real_name;
        gint uid;
    } users[] =
    {
        /* Root account */
        {"root",             "",          "root",                  0},
        /* Unprivileged account for greeters */
        {"lightdm",          "",          "",                    100},
        /* These accounts have a password */
        {"have-password1",   "password",  "Password User 1",    1000},
        {"have-password2",   "password",  "Password User 2",    1001},
        {"have-password3",   "password",  "Password User 3",    1002},
        {"have-password4",   "password",  "Password User 4",    1003},
        /* This account always prompts for a password, even if using the lightdm-autologin service */
        {"always-password",  "password",  "Password User 4",    1004},
        /* These accounts have no password */
        {"no-password1",     "",          "No Password User 1", 1005},
        {"no-password2",     "",          "No Password User 2", 1006},
        {"no-password3",     "",          "No Password User 3", 1007},
        {"no-password4",     "",          "No Password User 4", 1008},
        /* This account has a keyboard layout */
        {"have-layout",      "",          "Layout User",        1009},
        /* This account has a set of keyboard layouts */
        {"have-layouts",     "",          "Layouts User",       1010},
        /* This account has a language set */
        {"have-language",    "",          "Language User",      1011},
        /* This account has a preconfigured session */
        {"have-session",            "",   "Session User",       1012},
        /* This account has the home directory mounted on login */
        {"mount-home-dir",   "",          "Mounted Home Dir User", 1013},
        /* This account is denied access */
        {"denied",           "",          "Denied User",        1014},
        /* This account has expired */
        {"expired",          "",          "Expired User",       1015},
        /* This account needs a password change */
        {"new-authtok",      "",          "New Token User",     1016},
        /* This account is switched to change-user2 when authentication succeeds */
        {"change-user1",     "",          "Change User 1",      1017},
        {"change-user2",     "",          "Change User 2",      1018},
        /* This account switches to invalid-user when authentication succeeds */
        {"change-user-invalid", "",       "Invalid Change User", 1019},
        /* This account crashes on authentication */
        {"crash-authenticate", "",        "Crash Auth User",    1020},
        /* This account shows an informational prompt on login */
        {"info-prompt",      "password",  "Info Prompt",        1021},
        /* This account shows multiple informational prompts on login */
        {"multi-info-prompt","password",  "Multi Info Prompt",  1022},
        /* This account uses two factor authentication */
        {"two-factor",       "password",  "Two Factor",         1023},
        /* This account has a special group */
        {"group-member",     "password",  "Group Member",       1024},
        /* This account has the home directory created when the session starts */
        {"make-home-dir",    "",          "Make Home Dir User", 1025},
        /* This account fails to open a session */
        {"session-error",    "password",  "Session Error",      1026},
        /* This account can't establish credentials */
        {"cred-error",       "password",  "Cred Error",         1027},
        /* This account has expired credentials */
        {"cred-expired",     "password",  "Cred Expired",       1028},
        /* This account has cannot access their credentials */
        {"cred-unavail",     "password",  "Cred Unavail",       1029},
        /* This account sends informational messages for each PAM function that is called */
        {"log-pam",          "password",  "Log PAM",            1030},
        /* This account shows multiple prompts on login */
        {"multi-prompt",     "password",  "Multi Prompt",       1031},
        /* This account has an existing corrupt X authority */
        {"corrupt-xauth",    "password",  "Corrupt Xauthority", 1032},
        /* User to test properties */
        {"prop-user",        "",          "TEST",               1033},
        {NULL,               NULL,        NULL,                    0}
    };
    passwd_data = g_string_new ("");
    group_data = g_string_new ("");
    for (i = 0; users[i].user_name; i++)
    {
        GKeyFile *dmrc_file;
        gboolean save_dmrc = FALSE;

        if (strcmp (users[i].user_name, "mount-home-dir") != 0 && strcmp (users[i].user_name, "make-home-dir") != 0)
        {
            path = g_build_filename (home_dir, users[i].user_name, NULL);
            g_mkdir_with_parents (path, 0755);
            if (chown (path, users[i].uid, users[i].uid) < 0)
              g_debug ("chown (%s) failed: %s", path, strerror (errno));
            g_free (path);
        }

        dmrc_file = g_key_file_new ();
        if (strcmp (users[i].user_name, "have-session") == 0)
        {
            g_key_file_set_string (dmrc_file, "Desktop", "Session", "alternative");
            save_dmrc = TRUE;
        }
        if (strcmp (users[i].user_name, "have-layout") == 0)
        {
            g_key_file_set_string (dmrc_file, "Desktop", "Layout", "us");
            save_dmrc = TRUE;
        }
        if (strcmp (users[i].user_name, "have-layouts") == 0)
        {
            g_key_file_set_string (dmrc_file, "Desktop", "Layout", "ru");
            g_key_file_set_string (dmrc_file, "X-Accounts", "Layouts", "fr\toss;ru;");
            save_dmrc = TRUE;
        }
        if (strcmp (users[i].user_name, "have-language") == 0)
        {
            g_key_file_set_string (dmrc_file, "Desktop", "Language", "en_AU.utf8");
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

        /* Write corrupt X authority file */
        if (strcmp (users[i].user_name, "corrupt-xauth") == 0)
        {
            gchar data[1] = { 0xFF };

            path = g_build_filename (home_dir, users[i].user_name, ".Xauthority", NULL);
            g_file_set_contents (path, data, 1, NULL);
            chmod (path, S_IRUSR | S_IWUSR);
            g_free (path);
        }

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

    if (g_key_file_has_key (config, "test-runner-config", "timeout", NULL))
        status_timeout_ms = g_key_file_get_integer (config, "test-runner-config", "timeout", NULL) * 1000;

    /* Start D-Bus services */
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-upower", NULL))
        start_upower_daemon ();
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-console-kit", NULL))
        start_console_kit_daemon ();
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-login1", NULL))
        start_login1_daemon ();
    if (!g_key_file_get_boolean (config, "test-runner-config", "disable-accounts-service", NULL))
        start_accounts_service_daemon ();

    /* Listen for daemon bus events */
    if (g_key_file_get_boolean (config, "test-runner-config", "log-dbus", NULL))
    {
        g_dbus_connection_signal_subscribe (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                            "org.freedesktop.DisplayManager",
                                            "org.freedesktop.DBus.Properties",
                                            "PropertiesChanged",
                                            NULL,
                                            NULL,
                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                            properties_changed_cb,
                                            NULL,
                                            NULL);
        g_dbus_connection_signal_subscribe (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                            "org.freedesktop.DisplayManager",
                                            "org.freedesktop.DisplayManager",
                                            NULL,
                                            NULL,
                                            NULL,
                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                            dbus_signal_cb,
                                            NULL,
                                            NULL);
    }

    g_main_loop_run (loop);

    return EXIT_FAILURE;
}
