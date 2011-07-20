/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <fcntl.h>

#include "configuration.h"
#include "display-manager.h"
#include "xserver.h"
#include "user.h"
#include "pam-session.h"
#include "process.h"

static gchar *config_path = NULL;
static GMainLoop *loop = NULL;
static GTimer *log_timer;
static FILE *log_file;
static gboolean debug = FALSE;

static DisplayManager *display_manager = NULL;

static GDBusConnection *bus = NULL;
static guint bus_id;
static GDBusNodeInfo *seat_info;
static GHashTable *seat_bus_entries;
static guint seat_index = 0;
static GDBusNodeInfo *session_info;
static GHashTable *session_bus_entries;
static guint session_index = 0;

typedef struct
{
    gchar *path;
    gchar *parent_path;
    gchar *removed_signal;
    guint bus_id;
} BusEntry;

#define LDM_BUS_NAME "org.freedesktop.DisplayManager"

static void
log_cb (const gchar *log_domain, GLogLevelFlags log_level,
        const gchar *message, gpointer data)
{
    /* Log everything to a file */
    if (log_file) {
        const gchar *prefix;

        switch (log_level & G_LOG_LEVEL_MASK) {
        case G_LOG_LEVEL_ERROR:
            prefix = "ERROR:";
            break;
        case G_LOG_LEVEL_CRITICAL:
            prefix = "CRITICAL:";
            break;
        case G_LOG_LEVEL_WARNING:
            prefix = "WARNING:";
            break;
        case G_LOG_LEVEL_MESSAGE:
            prefix = "MESSAGE:";
            break;
        case G_LOG_LEVEL_INFO:
            prefix = "INFO:";
            break;
        case G_LOG_LEVEL_DEBUG:
            prefix = "DEBUG:";
            break;
        default:
            prefix = "LOG:";
            break;
        }

        fprintf (log_file, "[%+.2fs] %s %s\n", g_timer_elapsed (log_timer, NULL), prefix, message);
        fflush (log_file);
    }

    /* Only show debug if requested */
    if (log_level & G_LOG_LEVEL_DEBUG) {
        if (debug)
            g_log_default_handler (log_domain, log_level, message, data);
    }
    else
        g_log_default_handler (log_domain, log_level, message, data);    
}

static void
log_init (void)
{
    gchar *log_dir, *path;

    log_timer = g_timer_new ();

    /* Log to a file */
    log_dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    path = g_build_filename (log_dir, "lightdm.log", NULL);
    g_free (log_dir);

    log_file = fopen (path, "w");
    g_log_set_default_handler (log_cb, NULL);

    g_debug ("Logging to %s", path);
    g_free (path);
}

static void
signal_cb (Process *process, int signum)
{
    g_debug ("Caught %s signal, shutting down", g_strsignal (signum));
    display_manager_stop (display_manager);
}

static void
display_manager_stopped_cb (DisplayManager *display_manager)
{
    g_debug ("Stopping Light Display Manager");
    exit (EXIT_SUCCESS);
}

static Session *
get_session_for_cookie (const gchar *cookie, Seat **seat)
{
    GList *link;

    for (link = display_manager_get_seats (display_manager); link; link = link->next)
    {
        Seat *s = link->data;
        GList *l;

        for (l = seat_get_displays (s); l; l = l->next)
        {
            Display *display = l->data;
            Session *session;

            session = display_get_session (display);
            if (!session)
                continue;

            if (g_strcmp0 (session_get_cookie (session), cookie) == 0)
            {
                if (seat)
                    *seat = s;
                return session;
            }
        }
    }

    return NULL;
}

static GVariant *
handle_display_manager_get_property (GDBusConnection       *connection,
                                     const gchar           *sender,
                                     const gchar           *object_path,
                                     const gchar           *interface_name,
                                     const gchar           *property_name,
                                     GError               **error,
                                     gpointer               user_data)
{
    GVariant *result = NULL;

    if (g_strcmp0 (property_name, "Seats") == 0)
    {
        GVariantBuilder *builder;
        GHashTableIter iter;
        gpointer value;

        builder = g_variant_builder_new (G_VARIANT_TYPE ("ao"));
        g_hash_table_iter_init (&iter, seat_bus_entries);
        while (g_hash_table_iter_next (&iter, NULL, &value))
        {
            BusEntry *entry = value;
            g_variant_builder_add_value (builder, g_variant_new_object_path (entry->path));
        }
        result = g_variant_builder_end (builder);
        g_variant_builder_unref (builder);
    }
    else if (g_strcmp0 (property_name, "Sessions") == 0)
    {
        GVariantBuilder *builder;
        GHashTableIter iter;
        gpointer value;

        builder = g_variant_builder_new (G_VARIANT_TYPE ("ao"));
        g_hash_table_iter_init (&iter, session_bus_entries);
        while (g_hash_table_iter_next (&iter, NULL, &value))
        {
            BusEntry *entry = value;
            g_variant_builder_add_value (builder, g_variant_new_object_path (entry->path));
        }
        result = g_variant_builder_end (builder);
        g_variant_builder_unref (builder);
    }

    return result;
}

static void
handle_display_manager_call (GDBusConnection       *connection,
                             const gchar           *sender,
                             const gchar           *object_path,
                             const gchar           *interface_name,
                             const gchar           *method_name,
                             GVariant              *parameters,
                             GDBusMethodInvocation *invocation,
                             gpointer               user_data)
{
    if (g_strcmp0 (method_name, "GetSeatForCookie") == 0)
    {
        gchar *cookie;
        Seat *seat = NULL;
        BusEntry *entry = NULL;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
            return;

        g_variant_get (parameters, "(s)", &cookie);

        get_session_for_cookie (cookie, &seat);
        g_free (cookie);

        if (seat)
            entry = g_hash_table_lookup (seat_bus_entries, seat);
        if (entry)
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", entry->path));
        else // FIXME: Need to make proper error
            g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Unable to find seat for cookie");
    }
    else if (g_strcmp0 (method_name, "GetSessionForCookie") == 0)
    {
        gchar *cookie;
        Session *session;
        BusEntry *entry = NULL;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
            return;

        g_variant_get (parameters, "(s)", &cookie);

        session = get_session_for_cookie (cookie, NULL);
        g_free (cookie);
        if (session)
            entry = g_hash_table_lookup (session_bus_entries, session);
        if (entry)
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", entry->path));
        else // FIXME: Need to make proper error
            g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Unable to find session for cookie");
    }
}

static GVariant *
handle_seat_get_property (GDBusConnection       *connection,
                          const gchar           *sender,
                          const gchar           *object_path,
                          const gchar           *interface_name,
                          const gchar           *property_name,
                          GError               **error,
                          gpointer               user_data)
{
    Seat *seat = user_data;
    GVariant *result = NULL;

    if (g_strcmp0 (property_name, "CanSwitch") == 0)
        result = g_variant_new_boolean (seat_get_can_switch (seat));
    else if (g_strcmp0 (property_name, "Sessions") == 0)
    {
        GVariantBuilder *builder;
        GList *link;

        builder = g_variant_builder_new (G_VARIANT_TYPE ("ao"));
        for (link = seat_get_displays (seat); link; link = link->next)
        {
            Display *display = link->data;
            BusEntry *entry;
            entry = g_hash_table_lookup (session_bus_entries, display_get_session (display));
            if (entry)
                g_variant_builder_add_value (builder, g_variant_new_object_path (entry->path));
        }
        result = g_variant_builder_end (builder);
        g_variant_builder_unref (builder);
    }
  
    return result;
}

static void
handle_seat_call (GDBusConnection       *connection,
                  const gchar           *sender,
                  const gchar           *object_path,
                  const gchar           *interface_name,
                  const gchar           *method_name,
                  GVariant              *parameters,
                  GDBusMethodInvocation *invocation,
                  gpointer               user_data)
{
    Seat *seat = user_data;
  
    if (g_strcmp0 (method_name, "SwitchToGreeter") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("()")))
            return;

        seat_switch_to_greeter (seat);
        g_dbus_method_invocation_return_value (invocation, NULL);
    }
    else if (g_strcmp0 (method_name, "SwitchToUser") == 0)
    {
        gchar *username;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
            return;

        g_variant_get (parameters, "(s)", &username);
        seat_switch_to_user (seat, username);
        g_dbus_method_invocation_return_value (invocation, NULL);
        g_free (username);
    }
    else if (g_strcmp0 (method_name, "SwitchToGuest") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("()")))
            return;

        seat_switch_to_guest (seat);
        g_dbus_method_invocation_return_value (invocation, NULL);
    }
}

static GVariant *
handle_session_get_property (GDBusConnection       *connection,
                             const gchar           *sender,
                             const gchar           *object_path,
                             const gchar           *interface_name,
                             const gchar           *property_name,
                             GError               **error,
                             gpointer               user_data)
{
    Session *session = user_data;
    BusEntry *entry;

    entry = g_hash_table_lookup (session_bus_entries, session);
    if (g_strcmp0 (property_name, "Seat") == 0)
        return g_variant_new_object_path (entry ? entry->parent_path : "");
    else if (g_strcmp0 (property_name, "UserName") == 0)
        return g_variant_new_string (user_get_name (session_get_user (session)));

    return NULL;
}

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
}

static BusEntry *
bus_entry_new (const gchar *path, const gchar *parent_path, const gchar *removed_signal)
{
    BusEntry *entry;

    entry = g_malloc0 (sizeof (BusEntry));
    entry->path = g_strdup (path);
    entry->parent_path = g_strdup (parent_path);
    entry->removed_signal = g_strdup (removed_signal);

    return entry;
}

static void
bus_entry_free (gpointer data)
{
    BusEntry *entry = data;
    g_dbus_connection_unregister_object (bus, entry->bus_id);

    g_dbus_connection_emit_signal (bus,
                                   NULL,
                                   "/org/freedesktop/DisplayManager",
                                   "org.freedesktop.DisplayManager",
                                   entry->removed_signal,
                                   g_variant_new ("(o)", entry->path),
                                   NULL);

    g_free (entry->path);
    g_free (entry->parent_path);
    g_free (entry->removed_signal);
    g_free (entry);
}

static void
session_started_cb (Display *display, Seat *seat)
{
    static const GDBusInterfaceVTable session_vtable =
    {
        handle_session_call,
        handle_session_get_property
    };
    Session *session;
    gchar *path;
    BusEntry *seat_entry, *entry;

    session = display_get_session (display);
    if (session_get_is_greeter (session))
        return;

    path = g_strdup_printf ("/org/freedesktop/DisplayManager/Session%d", session_index);
    session_index++;

    seat_entry = g_hash_table_lookup (seat_bus_entries, seat);
    entry = bus_entry_new (path, seat_entry ? seat_entry->path : NULL, "SessionRemoved");
    g_free (path);
    g_hash_table_insert (session_bus_entries, session, entry);

    entry->bus_id = g_dbus_connection_register_object (bus,
                                                       entry->path,
                                                       session_info->interfaces[0],
                                                       &session_vtable,
                                                       g_object_ref (session), g_object_unref,
                                                       NULL);
    g_dbus_connection_emit_signal (bus,
                                   NULL,
                                   "/org/freedesktop/DisplayManager",
                                   "org.freedesktop.DisplayManager",
                                   "SessionAdded",
                                   g_variant_new ("(o)", entry->path),
                                   NULL);
}

static void
session_stopped_cb (Display *display)
{
    g_hash_table_remove (session_bus_entries, display_get_session (display));
}

static void
display_added_cb (Seat *seat, Display *display)
{
    g_signal_connect (display, "session-started", G_CALLBACK (session_started_cb), seat);
    g_signal_connect (display, "session-stopped", G_CALLBACK (session_stopped_cb), NULL);
}

static void
seat_added_cb (DisplayManager *display_manager, Seat *seat)
{
    static const GDBusInterfaceVTable seat_vtable =
    {
        handle_seat_call,
        handle_seat_get_property
    };
    gchar *path;
    BusEntry *entry;

    g_signal_connect (seat, "display-added", G_CALLBACK (display_added_cb), NULL);

    path = g_strdup_printf ("/org/freedesktop/DisplayManager/Seat%d", seat_index);
    seat_index++;

    entry = bus_entry_new (path, NULL, "SeatRemoved");
    g_free (path);
    g_hash_table_insert (seat_bus_entries, seat, entry);

    entry->bus_id = g_dbus_connection_register_object (bus,
                                                       entry->path,
                                                       seat_info->interfaces[0],
                                                       &seat_vtable,
                                                       g_object_ref (seat), g_object_unref,
                                                       NULL);
    g_dbus_connection_emit_signal (bus,
                                   NULL,
                                   "/org/freedesktop/DisplayManager",
                                   "org.freedesktop.DisplayManager",
                                   "SeatAdded",
                                   g_variant_new ("(o)", entry->path),
                                   NULL);
}

static void
seat_removed_cb (Seat *seat)
{
    g_hash_table_remove (seat_bus_entries, seat);
}

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    const gchar *display_manager_interface =
        "<node>"
        "  <interface name='org.freedesktop.DisplayManager'>"
        "    <property name='Seats' type='ao' access='read'/>"
        "    <property name='Sessions' type='ao' access='read'/>"
        "    <method name='GetSeatForCookie'>"
        "      <arg name='cookie' direction='in' type='s'/>"
        "      <arg name='seat' direction='out' type='o'/>"
        "    </method>"
        "    <method name='GetSessionForCookie'>"
        "      <arg name='cookie' direction='in' type='s'/>"
        "      <arg name='session' direction='out' type='o'/>"
        "    </method>"
        "    <signal name='SeatAdded'>"
        "      <arg name='seat' type='o'/>"
        "    </signal>"
        "    <signal name='SeatRemoved'>"
        "      <arg name='seat' type='o'/>"
        "    </signal>"
        "    <signal name='SessionAdded'>"
        "      <arg name='session' type='o'/>"
        "    </signal>"
        "    <signal name='SessionRemoved'>"
        "      <arg name='session' type='o'/>"
        "    </signal>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable display_manager_vtable =
    {
        handle_display_manager_call,
        handle_display_manager_get_property
    };
    const gchar *seat_interface =
        "<node>"
        "  <interface name='org.freedesktop.DisplayManager.Seat'>"
        "    <property name='CanSwitch' type='b' access='read'/>"
        "    <property name='Sessions' type='ao' access='read'/>"
        "    <method name='SwitchToGreeter'/>"
        "    <method name='SwitchToUser'>"
        "      <arg name='username' direction='in' type='s'/>"
        "    </method>"
        "    <method name='SwitchToGuest'/>"
        "  </interface>"
        "</node>";
    const gchar *session_interface =
        "<node>"
        "  <interface name='org.freedesktop.DisplayManager.Session'>"
        "    <property name='Seat' type='o' access='read'/>"
        "    <property name='UserName' type='s' access='read'/>"
        "  </interface>"
        "</node>";
    GDBusNodeInfo *display_manager_info;
    GList *link;

    bus = connection;

    display_manager_info = g_dbus_node_info_new_for_xml (display_manager_interface, NULL);
    g_assert (display_manager_info != NULL);
    seat_info = g_dbus_node_info_new_for_xml (seat_interface, NULL);
    g_assert (seat_info != NULL);
    session_info = g_dbus_node_info_new_for_xml (session_interface, NULL);
    g_assert (session_info != NULL);

    bus_id = g_dbus_connection_register_object (connection,
                                                "/org/freedesktop/DisplayManager",
                                                display_manager_info->interfaces[0],
                                                &display_manager_vtable,
                                                NULL, NULL,
                                                NULL);

    seat_bus_entries = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, bus_entry_free);
    session_bus_entries = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, bus_entry_free);

    g_signal_connect (display_manager, "seat-added", G_CALLBACK (seat_added_cb), NULL);
    g_signal_connect (display_manager, "seat-removed", G_CALLBACK (seat_removed_cb), NULL);
    for (link = display_manager_get_seats (display_manager); link; link = link->next)
        seat_added_cb (display_manager, (Seat *) link->data);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
    if (connection)
        g_printerr ("Failed to use bus name " LDM_BUS_NAME ", do you have appropriate permissions?\n");
    else
        g_printerr ("Failed to get system bus\n"); // FIXME: Could be session bus

    exit (EXIT_FAILURE);
}

static gchar *
path_make_absolute (gchar *path)
{
    gchar *cwd, *abs_path;
  
    if (!path)
        return NULL;

    if (g_path_is_absolute (path))
        return path;

    cwd = g_get_current_dir ();
    abs_path = g_build_filename (cwd, path, NULL);
    g_free (path);

    return abs_path;
}

int
main (int argc, char **argv)
{
    FILE *pid_file;
    GOptionContext *option_context;
    gboolean explicit_config = FALSE;
    gboolean test_mode = FALSE;
    gboolean no_root = FALSE;
    gchar *pid_path = "/var/run/lightdm.pid";
    gchar *xserver_command = NULL;
    gchar *passwd_path = NULL;
    gchar *xsessions_dir = NULL;
    gchar *xgreeters_dir = NULL;
    gchar *greeter_session = NULL;
    gchar *user_session = NULL;
    gchar *log_dir = NULL;
    gchar *run_dir = NULL;
    gchar *cache_dir = NULL;
    gchar *default_log_dir = g_strdup (LOG_DIR);
    gchar *default_run_dir = g_strdup (RUN_DIR);
    gchar *default_cache_dir = g_strdup (CACHE_DIR);
    gchar *minimum_display_number = NULL;
    gboolean show_version = FALSE;
    GOptionEntry options[] = 
    {
        { "config", 'c', 0, G_OPTION_ARG_STRING, &config_path,
          /* Help string for command line --config flag */
          N_("Use configuration file"), NULL },
        { "debug", 'd', 0, G_OPTION_ARG_NONE, &debug,
          /* Help string for command line --debug flag */
          N_("Print debugging messages"), NULL },
        { "test-mode", 0, 0, G_OPTION_ARG_NONE, &test_mode,
          /* Help string for command line --test-mode flag */
          N_("Alias for --no-root --minimum-display-number=50"), NULL },
        { "no-root", 0, 0, G_OPTION_ARG_NONE, &no_root,
          /* Help string for command line --no-root flag */
          N_("Run as unprivileged user, skipping things that require root access"), NULL },
        { "passwd-file", 0, 0, G_OPTION_ARG_STRING, &passwd_path,
          /* Help string for command line --use-passwd flag */
          N_("Use the given password file for authentication (for testing, requires --no-root)"), "FILE" },
        { "pid-file", 0, 0, G_OPTION_ARG_STRING, &pid_path,
          /* Help string for command line --pid-file flag */
          N_("File to write PID into"), "FILE" },
        { "xserver-command", 0, 0, G_OPTION_ARG_STRING, &xserver_command,
          /* Help string for command line --xserver-command flag */
          N_("Command to run X servers"), "COMMAND" },
        { "greeter-session", 0, 0, G_OPTION_ARG_STRING, &greeter_session,
          /* Help string for command line --greeter-session flag */
          N_("Greeter session"), "SESSION" },
        { "user-session", 0, 0, G_OPTION_ARG_STRING, &user_session,
          /* Help string for command line --user-session flag */
          N_("User session"), "SESSION" },
        { "minimum-display-number", 0, 0, G_OPTION_ARG_STRING, &minimum_display_number,
          /* Help string for command line --minimum-display-number flag */
          N_("Minimum display number to use for X servers"), "NUMBER" },
        { "xsessions-dir", 0, 0, G_OPTION_ARG_STRING, &xsessions_dir,
          /* Help string for command line --xsessions-dir flag */
          N_("Directory to load X sessions from"), "DIRECTORY" },
        { "xgreeters-dir", 0, 0, G_OPTION_ARG_STRING, &xgreeters_dir,
          /* Help string for command line --xgreeters-dir flag */
          N_("Directory to load X greeters from"), "DIRECTORY" },
        { "log-dir", 0, 0, G_OPTION_ARG_STRING, &log_dir,
          /* Help string for command line --log-dir flag */
          N_("Directory to write logs to"), "DIRECTORY" },
        { "run-dir", 0, 0, G_OPTION_ARG_STRING, &run_dir,
          /* Help string for command line --run-dir flag */
          N_("Directory to store running state"), "DIRECTORY" },
        { "cache-dir", 0, 0, G_OPTION_ARG_STRING, &cache_dir,
          /* Help string for command line --cache-dir flag */
          N_("Directory to cached information"), "DIRECTORY" },
        { "version", 'v', 0, G_OPTION_ARG_NONE, &show_version,
          /* Help string for command line --version flag */
          N_("Show release version"), NULL },
        { NULL }
    };
    GError *error = NULL;

    g_thread_init (NULL);
    g_type_init ();

    g_signal_connect (process_get_current (), "got-signal", G_CALLBACK (signal_cb), NULL);

    option_context = g_option_context_new (/* Arguments and description for --help test */
                                           _("- Display Manager"));
    g_option_context_add_main_entries (option_context, options, GETTEXT_PACKAGE);
    if (!g_option_context_parse (option_context, &argc, &argv, &error))
    {
        fprintf (stderr, "%s\n", error->message);
        fprintf (stderr, /* Text printed out when an unknown command-line argument provided */
                 _("Run '%s --help' to see a full list of available command line options."), argv[0]);
        fprintf (stderr, "\n");
        return EXIT_FAILURE;
    }
    g_clear_error (&error);
    if (config_path)
        explicit_config = TRUE;
    else
        config_path = g_strdup (DEFAULT_CONFIG_FILE);
    if (test_mode)
    {
        no_root = TRUE;
        g_free (minimum_display_number);
        minimum_display_number = g_strdup ("50");
    }

    if (!no_root && getuid () != 0)
    {
        g_printerr ("Only root can run Light Display Manager.  To run as a regular user for testing run with the --test-mode flag.\n");
        return EXIT_FAILURE;
    }

    /* If running inside an X server use Xephyr for display */
    if (getenv ("DISPLAY") && getuid () != 0)
    {
        gchar *xserver_path;

        xserver_path = g_find_program_in_path ("Xephyr");
        if (!xserver_path)
        {
            g_printerr ("Running inside an X server requires Xephyr to be installed but it cannot be found.  Please install it or update your PATH environment variable.\n");
            return EXIT_FAILURE;
        }
        g_free (xserver_path);
    }

    /* Don't allow to be run as root and use a password file (asking for danger!) */
    if (getuid () == 0 && passwd_path)
    {
        g_printerr ("Only allowed to use --passwd-file when running with --no-root.\n"); 
        return EXIT_FAILURE;
    }

    if (show_version)
    {
        /* NOTE: Is not translated so can be easily parsed */
        g_printerr ("%s %s\n", LIGHTDM_BINARY, VERSION);
        return EXIT_SUCCESS;
    }

    /* Write PID file */
    pid_file = fopen (pid_path, "w");
    if (pid_file)
    {
        fprintf (pid_file, "%d\n", getpid ());
        fclose (pid_file);
    }

    /* Always use absolute directories as child processes may run from different locations */
    xsessions_dir = path_make_absolute (xsessions_dir);
    xgreeters_dir = path_make_absolute (xgreeters_dir);

    /* If not running as root write output to directories we control */
    if (getuid () != 0)
    {
        g_free (default_log_dir);
        default_log_dir = g_build_filename (g_get_user_cache_dir (), "lightdm", "log", NULL);
        g_free (default_run_dir);
        default_run_dir = g_build_filename (g_get_user_cache_dir (), "lightdm", "run", NULL);
        g_free (default_cache_dir);
        default_cache_dir = g_build_filename (g_get_user_cache_dir (), "lightdm", "cache", NULL);
    }

    /* Load config file */
    if (!config_load_from_file (config_get_instance (), config_path, &error))
    {
        if (explicit_config || !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
            g_printerr ("Failed to load configuration from %s: %s\n", config_path, error->message);
            exit (EXIT_FAILURE);
        }

        /* Add in some default configuration */
        config_set_string (config_get_instance (), "LightDM", "seats", "seat-0");
    }
    g_clear_error (&error);

    /* Set default values */
    if (!config_has_key (config_get_instance (), "SeatDefaults", "type"))
        config_set_string (config_get_instance (), "SeatDefaults", "type", "xlocal");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "xserver-command"))
        config_set_string (config_get_instance (), "SeatDefaults", "xserver-command", "X");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "xsessions-directory"))
        config_set_string (config_get_instance (), "SeatDefaults", "xsessions-directory", XSESSIONS_DIR);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "xgreeters-directory"))
        config_set_string (config_get_instance (), "SeatDefaults", "xgreeters-directory", XGREETERS_DIR);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "greeter-session"))
        config_set_string (config_get_instance (), "SeatDefaults", "greeter-session", GREETER_SESSION);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "user-session"))
        config_set_string (config_get_instance (), "SeatDefaults", "user-session", USER_SESSION);
    if (!config_has_key (config_get_instance (), "LightDM", "log-directory"))
        config_set_string (config_get_instance (), "LightDM", "log-directory", default_log_dir);    
    g_free (default_log_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "run-directory"))
        config_set_string (config_get_instance (), "LightDM", "run-directory", default_run_dir);
    g_free (default_run_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "cache-directory"))
        config_set_string (config_get_instance (), "LightDM", "cache-directory", default_cache_dir);
    g_free (default_cache_dir);

    /* Override defaults */
    if (minimum_display_number)
        config_set_integer (config_get_instance (), "LightDM", "minimum-display-number", atoi (minimum_display_number));
    g_free (minimum_display_number);
    if (log_dir)
        config_set_string (config_get_instance (), "LightDM", "log-directory", log_dir);
    g_free (log_dir);
    if (run_dir)
        config_set_string (config_get_instance (), "LightDM", "run-directory", run_dir);
    g_free (run_dir);
    if (cache_dir)
        config_set_string (config_get_instance (), "LightDM", "cache-directory", cache_dir);
    g_free (cache_dir);
    if (xserver_command)
        config_set_string (config_get_instance (), "SeatDefaults", "xserver-command", xserver_command);
    g_free (xserver_command);
    if (greeter_session)
        config_set_string (config_get_instance (), "SeatDefaults", "greeter-session", greeter_session);
    g_free (greeter_session);
    if (user_session)
        config_set_string (config_get_instance (), "SeatDefaults", "user-session", user_session);
    g_free (user_session);
    if (xsessions_dir)
        config_set_string (config_get_instance (), "SeatDefaults", "xsessions-directory", xsessions_dir);
    g_free (xsessions_dir);
    if (xgreeters_dir)
        config_set_string (config_get_instance (), "SeatDefaults", "xgreeters-directory", xgreeters_dir);
    g_free (xgreeters_dir);

    /* Create run and cache directories */
    g_mkdir_with_parents (config_get_string (config_get_instance (), "LightDM", "log-directory"), S_IRWXU | S_IXGRP | S_IXOTH);  
    g_mkdir_with_parents (config_get_string (config_get_instance (), "LightDM", "run-directory"), S_IRWXU | S_IXGRP | S_IXOTH);
    g_mkdir_with_parents (config_get_string (config_get_instance (), "LightDM", "cache-directory"), S_IRWXU | S_IXGRP | S_IXOTH);

    loop = g_main_loop_new (NULL, FALSE);

    log_init ();

    g_debug ("Starting Light Display Manager %s, UID=%i PID=%i", VERSION, getuid (), getpid ());

    g_bus_own_name (getuid () == 0 ? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION,
                    LDM_BUS_NAME,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    bus_acquired_cb,
                    NULL,
                    name_lost_cb,
                    NULL,
                    NULL);

    if (getuid () != 0)
        g_debug ("Running in user mode");
    if (passwd_path)
    {
        g_debug ("Using password file '%s' for authentication", passwd_path);
        user_set_use_passwd_file (passwd_path);
        pam_session_set_use_passwd_file (passwd_path);
    }
    if (getenv ("DISPLAY"))
        g_debug ("Using Xephyr for X servers");

    g_debug ("Loaded configuration from %s", config_path);

    display_manager = display_manager_new ();
    g_signal_connect (display_manager, "stopped", G_CALLBACK (display_manager_stopped_cb), NULL);

    display_manager_start (display_manager);

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
