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
#include <glib.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <fcntl.h>

#include "configuration.h"
#include "display-manager.h"
#include "xserver.h"
#include "user.h"
#include "pam-session.h"
#include "child-process.h"

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

typedef struct
{
    gchar *path;
    guint bus_id;
} SeatBusEntry;

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
    log_dir = config_get_string (config_get_instance (), "Directories", "log-directory");
    g_mkdir_with_parents (log_dir, 0755);
    path = g_build_filename (log_dir, "lightdm.log", NULL);
    g_free (log_dir);

    log_file = fopen (path, "w");
    g_log_set_default_handler (log_cb, NULL);

    g_debug ("Logging to %s", path);
    g_free (path);
}

static void
signal_cb (ChildProcess *process, int signum)
{
    /* Quit when all child processes have ended */
    g_debug ("Caught %s signal, shutting down", g_strsignal (signum));

    display_manager_stop (display_manager);
}

static void
display_manager_stopped_cb (DisplayManager *display_manager)
{
    g_debug ("Stopping Light Display Manager");
    exit (EXIT_SUCCESS);
}

static Seat *
get_seat_for_cookie (const gchar *cookie)
{
    GList *link;

    for (link = display_manager_get_seats (display_manager); link; link = link->next)
    {
        Seat *seat = link->data;
        GList *l;
      
        for (l = seat_get_displays (seat); l; l = l->next)
        {
            Display *display = l->data;
            Session *session;
          
            session = display_get_session (display);
            if (!session)
                continue;
          
            if (g_strcmp0 (session_get_cookie (session), cookie) == 0)
                return seat;
        }
    }

    return NULL;
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
    if (g_strcmp0 (method_name, "GetSeats") == 0)
    {
        GList *link;
        GVariantBuilder *builder;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("()")))
            return;

        builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
        for (link = display_manager_get_seats (display_manager); link; link = link->next)
        {
            Seat *seat = link->data;
            SeatBusEntry *entry;

            entry = g_hash_table_lookup (seat_bus_entries, seat);
            g_variant_builder_add_value (builder, g_variant_new_object_path (entry->path));
        }

        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(ao)", builder, NULL));
        g_variant_builder_unref (builder);
    }
    else if (g_strcmp0 (method_name, "GetSeatForCookie") == 0)
    {
        gchar *cookie;
        Seat *seat;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
            return;

        g_variant_get (parameters, "(s)", &cookie);

        seat = get_seat_for_cookie (cookie);
        g_free (cookie);

        if (seat)
        {
            SeatBusEntry *entry = g_hash_table_lookup (seat_bus_entries, seat);
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", entry->path));
        }
        else // FIXME: Need to make proper error
            g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Unable to find seat for cookie");
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

    if (g_strcmp0 (property_name, "CanSwitch") == 0)
        return g_variant_new_boolean (seat_get_can_switch (seat));
  
    return NULL;
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

static void
seat_added_cb (Seat *seat)
{
    static const GDBusInterfaceVTable seat_vtable =
    {
        handle_seat_call,
        handle_seat_get_property
    };
    SeatBusEntry *entry;

    entry = g_malloc (sizeof (SeatBusEntry));
    entry->path = g_strdup_printf ("/org/freedesktop/DisplayManager/Seat%d", seat_index);
    seat_index++;
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
                                   "org.freedesktop.DisplayManager.Seat",
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
seat_bus_entry_unref (gpointer data)
{
    SeatBusEntry *entry = data;
    g_dbus_connection_unregister_object (bus, entry->bus_id);

    g_dbus_connection_emit_signal (bus,
                                   NULL,
                                   "/org/freedesktop/DisplayManager",
                                   "org.freedesktop.DisplayManager.Seat",
                                   "SeatRemoved",
                                   g_variant_new ("(o)", entry->path),
                                   NULL);

    g_free (entry->path);
    g_free (entry);
}

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    const gchar *display_manager_interface =
        "<node>"
        "  <interface name='org.freedesktop.DisplayManager'>"
        "    <method name='GetSeats'>"
        "      <arg name='seats' direction='out' type='ao'/>"
        "    </method>"
        "    <method name='GetSeatForCookie'>"
        "      <arg name='cookie' direction='in' type='s'/>"
        "      <arg name='seat' direction='out' type='o'/>"
        "    </method>"
        "    <signal name='SeatAdded'>"
        "      <arg name='seat' type='o'/>"
        "    </signal>"
        "    <signal name='SeatRemoved'>"
        "      <arg name='seat' type='o'/>"
        "    </signal>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable display_manager_vtable =
    {
        handle_display_manager_call,
    };
    const gchar *seat_interface =
        "<node>"
        "  <interface name='org.freedesktop.DisplayManager.Seat'>"
        "    <property name='CanSwitch' type='b' access='read'/>"
        "    <method name='SwitchToGreeter'/>"
        "    <method name='SwitchToUser'>"
        "      <arg name='username' direction='in' type='s'/>"
        "    </method>"
        "    <method name='SwitchToGuest'/>"
        "  </interface>"
        "</node>";
    GDBusNodeInfo *display_manager_info;
    GList *link;

    bus = connection;

    display_manager_info = g_dbus_node_info_new_for_xml (display_manager_interface, NULL);
    g_assert (display_manager_info != NULL);
    seat_info = g_dbus_node_info_new_for_xml (seat_interface, NULL);
    g_assert (display_manager_info != NULL);

    bus_id = g_dbus_connection_register_object (connection,
                                                "/org/freedesktop/DisplayManager",
                                                display_manager_info->interfaces[0],
                                                &display_manager_vtable,
                                                NULL, NULL,
                                                NULL);

    seat_bus_entries = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, seat_bus_entry_unref);

    g_signal_connect (display_manager, "seat-added", G_CALLBACK (seat_added_cb), NULL);
    g_signal_connect (display_manager, "seat-removed", G_CALLBACK (seat_removed_cb), NULL);
    for (link = display_manager_get_seats (display_manager); link; link = link->next)
        seat_added_cb ((Seat *) link->data);
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
    gchar *xserver_command = g_strdup ("X");
    gchar *passwd_path = NULL;
    gchar *pid_path = "/var/run/lightdm.pid";
    gchar *greeter_session = g_strdup (GREETER_SESSION);
    gchar *xsessions_dir = g_strdup (XSESSIONS_DIR);
    gchar *run_dir = g_strdup (RUN_DIR);
    gchar *cache_dir = g_strdup (CACHE_DIR);
    gchar *user_session = g_strdup (user_session);
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
        { "run-dir", 0, 0, G_OPTION_ARG_STRING, &run_dir,
          /* Help string for command line --run-dir flag */
          N_("Directory to store run information"), "DIRECTORY" },
        { "cache-dir", 0, 0, G_OPTION_ARG_STRING, &cache_dir,
          /* Help string for command line --cache-dir flag */
          N_("Directory to cache information"), "DIRECTORY" },
        { "xsessions-dir", 0, 0, G_OPTION_ARG_STRING, &xsessions_dir,
          /* Help string for command line --xsessions-dir flag */
          N_("Directory to load X sessions from"), "DIRECTORY" },
        { "version", 'v', 0, G_OPTION_ARG_NONE, &show_version,
          /* Help string for command line --version flag */
          N_("Show release version"), NULL },
        { NULL }
    };
    GError *error = NULL;

    g_thread_init (NULL);
    g_type_init ();

    g_signal_connect (child_process_get_parent (), "got-signal", G_CALLBACK (signal_cb), NULL);

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
        minimum_display_number = g_strdup ("50");
    }
    if (show_version)
    {
        /* NOTE: Is not translated so can be easily parsed */
        g_printerr ("%s %s\n", LIGHTDM_BINARY, VERSION);
        return EXIT_SUCCESS;
    }

    /* Always use absolute directories as child processes may run from different locations */
    xsessions_dir = path_make_absolute (xsessions_dir);

    /* Check if root */
    if (!no_root && getuid () != 0)
    {
        g_printerr ("Only root can run Light Display Manager.  To run as a regular user for testing run with the --test-mode flag.\n");
        return EXIT_FAILURE;
    }

    /* Check if requiring Xephyr */
    if (getenv ("DISPLAY"))
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
    if (!no_root && passwd_path)
    {
        g_printerr ("Only allowed to use --passwd-file when running with --no-root.\n"); 
        return EXIT_FAILURE;
    }

    /* Write PID file */
    pid_file = fopen (pid_path, "w");
    if (pid_file)
    {
        fprintf (pid_file, "%d\n", getpid ());
        fclose (pid_file);
    }

    /* Create run and cache directories */
    g_mkdir_with_parents (run_dir, S_IRWXU | S_IXGRP | S_IXOTH);
    g_mkdir_with_parents (cache_dir, S_IRWXU | S_IXGRP | S_IXOTH);

    loop = g_main_loop_new (NULL, FALSE);

    g_bus_own_name (no_root ? G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM,
                    LDM_BUS_NAME,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    bus_acquired_cb,
                    NULL,
                    name_lost_cb,
                    NULL,
                    NULL);

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
    config_set_string (config_get_instance (), "SeatDefaults", "xserver-command", xserver_command);
    config_set_string (config_get_instance (), "SeatDefaults", "greeter-session", greeter_session);
    config_set_string (config_get_instance (), "SeatDefaults", "user-session", user_session);
    config_set_string (config_get_instance (), "Directories", "log-directory", LOG_DIR);
    config_set_string (config_get_instance (), "Directories", "run-directory", run_dir);
    config_set_string (config_get_instance (), "Directories", "cache-directory", cache_dir);
    config_set_string (config_get_instance (), "Directories", "xsessions-directory", xsessions_dir);

    if (minimum_display_number)
        config_set_integer (config_get_instance (), "LightDM", "minimum-display-number", atoi (minimum_display_number));

    if (no_root)
    {
        gchar *path;

        path = g_build_filename (g_get_user_cache_dir (), "lightdm", "run", NULL);
        config_set_string (config_get_instance (), "Directories", "run-directory", path);
        g_free (path);

        path = g_build_filename (g_get_user_cache_dir (), "lightdm", "cache", NULL);
        config_set_string (config_get_instance (), "Directories", "cache-directory", path);
        g_free (path);

        path = g_build_filename (g_get_user_cache_dir (), "lightdm", "log", NULL);
        config_set_string (config_get_instance (), "Directories", "log-directory", path);
        g_free (path);
    }

    log_init ();

    g_debug ("Starting Light Display Manager %s, UID=%i PID=%i", VERSION, getuid (), getpid ());

    if (no_root)
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
