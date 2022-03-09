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
#include <sys/stat.h>
#include <errno.h>

#include "configuration.h"
#include "display-manager.h"
#include "display-manager-service.h"
#include "xdmcp-server.h"
#include "vnc-server.h"
#include "seat-xdmcp-session.h"
#include "seat-xvnc.h"
#include "x-server.h"
#include "process.h"
#include "session-child.h"
#include "shared-data-manager.h"
#include "user-list.h"
#include "login1.h"
#include "log-file.h"

static gchar *config_path = NULL;
static GMainLoop *loop = NULL;
static GTimer *log_timer;
static int log_fd = -1;
static gboolean debug = FALSE;

static DisplayManager *display_manager = NULL;
static DisplayManagerService *display_manager_service = NULL;
static XDMCPServer *xdmcp_server = NULL;
static guint xdmcp_client_count = 0;
static VNCServer *vnc_server = NULL;
static guint vnc_client_count = 0;
static gint exit_code = EXIT_SUCCESS;

static gboolean update_login1_seat (Login1Seat *login1_seat);

static void
log_cb (const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer data)
{
    const gchar *prefix;
    switch (log_level & G_LOG_LEVEL_MASK)
    {
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

    g_autofree gchar *text = g_strdup_printf ("[%+.2fs] %s %s\n", g_timer_elapsed (log_timer, NULL), prefix, message);

    /* Log everything to a file */
    if (log_fd >= 0)
    {
        ssize_t n_written = write (log_fd, text, strlen (text));
        if (n_written < 0)
            ; /* Check result so compiler doesn't warn about it */
    }

    /* Log to stderr if requested */
    if (debug)
        g_printerr ("%s", text);
    else
        g_log_default_handler (log_domain, log_level, message, data);
}

static void
log_init (void)
{
    log_timer = g_timer_new ();

    /* Log to a file */
    g_autofree gchar *log_dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    g_autofree gchar *path = g_build_filename (log_dir, "lightdm.log", NULL);

    gboolean backup_logs = config_get_boolean (config_get_instance (), "LightDM", "backup-logs");
    log_fd = log_file_open (path, backup_logs ? LOG_MODE_BACKUP_AND_TRUNCATE : LOG_MODE_APPEND);
    fcntl (log_fd, F_SETFD, FD_CLOEXEC);
    g_log_set_default_handler (log_cb, NULL);

    g_debug ("Logging to %s", path);
}

static GList*
get_config_sections (const gchar *seat_name)
{
    /* Load seat defaults first */
    GList *config_sections = g_list_append (NULL, g_strdup ("Seat:*"));

    g_auto(GStrv) groups = config_get_groups (config_get_instance ());
    for (gchar **i = groups; *i; i++)
    {
        if (g_str_has_prefix (*i, "Seat:") && strcmp (*i, "Seat:*") != 0)
        {
            const gchar *seat_name_glob = *i + strlen ("Seat:");
            if (g_pattern_match_simple (seat_name_glob, seat_name ? seat_name : ""))
                config_sections = g_list_append (config_sections, g_strdup (*i));
        }
    }

    return config_sections;
}

static void
set_seat_properties (Seat *seat, const gchar *seat_name)
{
    GList *sections = get_config_sections (seat_name);
    for (GList *link = sections; link; link = link->next)
    {
        const gchar *section = link->data;
        g_auto(GStrv) keys = NULL;

        keys = config_get_keys (config_get_instance (), section);

        l_debug (seat, "Loading properties from config section %s", section);
        for (gint i = 0; keys && keys[i]; i++)
        {
            g_autofree gchar *value = config_get_string (config_get_instance (), section, keys[i]);
            seat_set_property (seat, keys[i], value);
        }
    }
    g_list_free_full (sections, g_free);
}

static void
signal_cb (Process *process, int signum)
{
    switch (signum)
    {
    case SIGINT:
    case SIGTERM:
        g_debug ("Caught %s signal, shutting down", g_strsignal (signum));
        display_manager_stop (display_manager);
        // FIXME: Stop XDMCP server
        break;
    case SIGUSR1:
    case SIGUSR2:
    case SIGHUP:
        break;
    }
}

static void
display_manager_stopped_cb (DisplayManager *display_manager)
{
    g_debug ("Stopping daemon");
    g_main_loop_quit (loop);
}

static Seat *
create_seat (const gchar *module_name, const gchar *name)
{
    if (strcmp (module_name, "xlocal") == 0) {
        g_warning ("Seat type 'xlocal' is deprecated, use 'type=local' instead");
        module_name = "local";
    }

    Seat *seat = seat_new (module_name);
    if (!seat)
        return NULL;

    seat_set_name (seat, name);
    return seat;
}

static Seat *
service_add_xlocal_seat_cb (DisplayManagerService *service, gint display_number)
{
    g_debug ("Adding local X seat :%d", display_number);

    g_autoptr(Seat) seat = create_seat ("xremote", "xremote0"); // FIXME: What to use for a name?
    if (!seat)
        return NULL;

    set_seat_properties (seat, NULL);
    g_autofree gchar *display_number_string = g_strdup_printf ("%d", display_number);
    seat_set_property (seat, "xserver-display-number", display_number_string);

    if (!display_manager_add_seat (display_manager, seat))
        return NULL;

    return g_steal_pointer (&seat);
}

static void
display_manager_seat_removed_cb (DisplayManager *display_manager, Seat *seat)
{
    /* If we have fallback types registered for the seat, let's try them
       before giving up. */
    g_auto(GStrv) types = seat_get_string_list_property (seat, "type");
    g_autoptr(GString) next_types = g_string_new ("");
    g_autoptr(Seat) next_seat = NULL;
    for (gchar **iter = types; iter && *iter; iter++)
    {
        if (iter == types)
            continue; // skip first one, that is our current seat type

        if (!next_seat)
        {
            next_seat = create_seat (*iter, seat_get_name (seat));
            g_string_assign (next_types, *iter);
        }
        else
        {
            // Build up list of types to try next time
            g_string_append_c (next_types, ';');
            g_string_append (next_types, *iter);
        }
    }

    if (next_seat)
    {
        set_seat_properties (next_seat, seat_get_name (seat));

        // We set this manually on default seat.  Let's port it over if needed.
        if (seat_get_boolean_property (seat, "exit-on-failure"))
            seat_set_property (next_seat, "exit-on-failure", "true");

        seat_set_property (next_seat, "type", next_types->str);

        display_manager_add_seat (display_manager, next_seat);
    }
    else if (seat_get_boolean_property (seat, "exit-on-failure"))
    {
        g_debug ("Required seat has stopped");
        exit_code = EXIT_FAILURE;
        display_manager_stop (display_manager);
    }
}

static gboolean
xdmcp_session_cb (XDMCPServer *server, XDMCPSession *session)
{
    g_autoptr(SeatXDMCPSession) seat = seat_xdmcp_session_new (session);

    g_autofree gchar *name = g_strdup_printf ("xdmcp%d", xdmcp_client_count);
    xdmcp_client_count++;

    seat_set_name (SEAT (seat), name);
    set_seat_properties (SEAT (seat), NULL);
    return display_manager_add_seat (display_manager, SEAT (seat));
}

static void
vnc_connection_cb (VNCServer *server, GSocket *connection)
{
    g_autoptr(SeatXVNC) seat = seat_xvnc_new (connection);

    g_autofree gchar *name = g_strdup_printf ("vnc%d", vnc_client_count);
    vnc_client_count++;

    seat_set_name (SEAT (seat), name);
    set_seat_properties (SEAT (seat), NULL);
    display_manager_add_seat (display_manager, SEAT (seat));
}

static void
start_display_manager (void)
{
    display_manager_start (display_manager);

    /* Start the XDMCP server */
    if (config_get_boolean (config_get_instance (), "XDMCPServer", "enabled"))
    {
        xdmcp_server = xdmcp_server_new ();
        if (config_has_key (config_get_instance (), "XDMCPServer", "port"))
        {
            gint port;
            port = config_get_integer (config_get_instance (), "XDMCPServer", "port");
            if (port > 0)
                xdmcp_server_set_port (xdmcp_server, port);
        }
        g_autofree gchar *listen_address = config_get_string (config_get_instance (), "XDMCPServer", "listen-address");
        xdmcp_server_set_listen_address (xdmcp_server, listen_address);
        g_autofree gchar *hostname = config_get_string (config_get_instance (), "XDMCPServer", "hostname");
        xdmcp_server_set_hostname (xdmcp_server, hostname);
        g_signal_connect (xdmcp_server, XDMCP_SERVER_SIGNAL_NEW_SESSION, G_CALLBACK (xdmcp_session_cb), NULL);

        g_autofree gchar *key_name = config_get_string (config_get_instance (), "XDMCPServer", "key");
        g_autofree gchar *key = NULL;
        if (key_name)
        {
            g_autofree gchar *path = g_build_filename (config_get_directory (config_get_instance ()), "keys.conf", NULL);

            g_autoptr(GKeyFile) keys = g_key_file_new ();
            g_autoptr(GError) error = NULL;
            gboolean result = g_key_file_load_from_file (keys, path, G_KEY_FILE_NONE, &error);
            if (error)
                g_warning ("Unable to load keys from %s: %s", path, error->message);

            if (result)
            {
                if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                    key = g_key_file_get_string (keys, "keyring", key_name, NULL);
                else
                    g_warning ("Key %s not defined", key_name);
            }
        }
        if (key)
            xdmcp_server_set_key (xdmcp_server, key);

        if (key_name && !key)
        {
            exit_code = EXIT_FAILURE;
            display_manager_stop (display_manager);
            return;
        }
        else
        {
            g_debug ("Starting XDMCP server on UDP/IP port %d", xdmcp_server_get_port (xdmcp_server));
            xdmcp_server_start (xdmcp_server);
        }
    }

    /* Start the VNC server */
    if (config_get_boolean (config_get_instance (), "VNCServer", "enabled"))
    {
        g_autofree gchar *path = g_find_program_in_path ("Xvnc");
        if (path)
        {
            vnc_server = vnc_server_new ();
            if (config_has_key (config_get_instance (), "VNCServer", "port"))
            {
                gint port = config_get_integer (config_get_instance (), "VNCServer", "port");
                if (port > 0)
                    vnc_server_set_port (vnc_server, port);
            }
            g_autofree gchar *listen_address = config_get_string (config_get_instance (), "VNCServer", "listen-address");
            vnc_server_set_listen_address (vnc_server, listen_address);
            g_signal_connect (vnc_server, VNC_SERVER_SIGNAL_NEW_CONNECTION, G_CALLBACK (vnc_connection_cb), NULL);

            g_debug ("Starting VNC server on TCP/IP port %d", vnc_server_get_port (vnc_server));
            vnc_server_start (vnc_server);
        }
        else
            g_warning ("Can't start VNC server, Xvnc is not in the path");
    }
}
static void
service_ready_cb (DisplayManagerService *service)
{
    start_display_manager ();
}

static void
service_name_lost_cb (DisplayManagerService *service)
{
    exit (EXIT_FAILURE);
}

static gboolean
add_login1_seat (Login1Seat *login1_seat)
{
    const gchar *seat_name = login1_seat_get_id (login1_seat);
    g_debug ("New seat added from logind: %s", seat_name);
    gboolean is_seat0 = strcmp (seat_name, "seat0") == 0;

    GList *config_sections = get_config_sections (seat_name);
    g_auto(GStrv) types = NULL;
    for (GList *link = g_list_last (config_sections); link; link = link->prev)
    {
        gchar *config_section = link->data;
        types = config_get_string_list (config_get_instance (), config_section, "type");
        if (types)
            break;
    }
    g_list_free_full (config_sections, g_free);

    g_autoptr(Seat) seat = NULL;
    for (gchar **type = types; !seat && type && *type; type++)
        seat = create_seat (*type, seat_name);

    if (seat)
    {
        set_seat_properties (seat, seat_name);

        if (!login1_seat_get_can_multi_session (login1_seat))
        {
            g_debug ("Seat %s has property CanMultiSession=no", seat_name);
            /* XXX: uncomment this line after bug #1371250 is closed.
            seat_set_property (seat, "allow-user-switching", "false"); */
        }

        if (is_seat0)
            seat_set_property (seat, "exit-on-failure", "true");
    }
    else
    {
        g_debug ("Unable to create seat: %s", seat_name);
        return FALSE;
    }

    gboolean started = display_manager_add_seat (display_manager, seat);
    if (!started)
        g_debug ("Failed to start seat: %s", seat_name);

    return started;
}

static void
remove_login1_seat (Login1Seat *login1_seat)
{
    Seat *seat = display_manager_get_seat (display_manager, login1_seat_get_id (login1_seat));
    if (seat)
        seat_stop (seat);
}

static void
seat_stopped_cb (Seat *seat, Login1Seat *login1_seat)
{
    update_login1_seat (login1_seat);
    g_signal_handlers_disconnect_matched (seat, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, login1_seat);
}

static gboolean
update_login1_seat (Login1Seat *login1_seat)
{
    if (!config_get_boolean (config_get_instance (), "LightDM", "logind-check-graphical") ||
        login1_seat_get_can_graphical (login1_seat))
    {
        /* Wait for existing seat to stop or ignore if we already have a valid seat */
        Seat *seat = display_manager_get_seat (display_manager, login1_seat_get_id (login1_seat));
        if (seat)
        {
            if (seat_get_is_stopping (seat))
                g_signal_connect (seat, SEAT_SIGNAL_STOPPED, G_CALLBACK (seat_stopped_cb), login1_seat);
            return TRUE;
        }

        return add_login1_seat (login1_seat);
    }
    else
    {
        remove_login1_seat (login1_seat);
        return TRUE;
    }
}

static void
login1_can_graphical_changed_cb (Login1Seat *login1_seat)
{
    g_debug ("Seat %s changes graphical state to %s", login1_seat_get_id (login1_seat), login1_seat_get_can_graphical (login1_seat) ? "true" : "false");
    update_login1_seat (login1_seat);
}

static void
login1_active_session_changed_cb (Login1Seat *login1_seat, const gchar *login1_session_id)
{
    g_debug ("Seat %s changes active session to %s", login1_seat_get_id (login1_seat), login1_session_id);

    Seat *seat = display_manager_get_seat (display_manager, login1_seat_get_id (login1_seat));
    if (seat)
    {
        Session *active_session = seat_get_expected_active_session (seat);
        if (active_session != NULL &&
            g_strcmp0 (login1_session_id, session_get_login1_session_id (active_session)) == 0)
        {
            // Session is already active
            g_debug ("Session %s is already active", login1_session_id);
            return;
        }

        active_session = seat_find_session_by_login1_id (seat, login1_session_id);
        if (active_session != NULL)
        {
            g_debug ("Activating session %s", login1_session_id);
            seat_set_externally_activated_session (seat, active_session);
            return;

        }
    }
}

static gboolean
login1_add_seat (Login1Seat *login1_seat)
{
    if (config_get_boolean (config_get_instance (), "LightDM", "logind-check-graphical"))
        g_signal_connect (login1_seat, "can-graphical-changed", G_CALLBACK (login1_can_graphical_changed_cb), NULL);

    g_signal_connect (login1_seat, LOGIN1_SIGNAL_ACTIVE_SESION_CHANGED, G_CALLBACK (login1_active_session_changed_cb), NULL);

    return update_login1_seat (login1_seat);
}

static void
login1_service_seat_added_cb (Login1Service *service, Login1Seat *login1_seat)
{
    if (login1_seat_get_can_graphical (login1_seat))
        g_debug ("Seat %s added from logind", login1_seat_get_id (login1_seat));
    else
        g_debug ("Seat %s added from logind without graphical output", login1_seat_get_id (login1_seat));

    login1_add_seat (login1_seat);
}

static void
login1_service_seat_removed_cb (Login1Service *service, Login1Seat *login1_seat)
{
    g_debug ("Seat %s removed from logind", login1_seat_get_id (login1_seat));
    g_signal_handlers_disconnect_matched (login1_seat, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, login1_can_graphical_changed_cb, NULL);
    g_signal_handlers_disconnect_matched (login1_seat, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, login1_active_session_changed_cb, NULL);
    remove_login1_seat (login1_seat);
}

int
main (int argc, char **argv)
{
    /* Disable the SIGPIPE handler - this is a stupid Unix hangover behaviour.
     * We will handle pipes / sockets being closed instead of having the whole daemon be killed...
     * http://stackoverflow.com/questions/8369506/why-does-sigpipe-exist
     * Similar case for SIGHUP.
     */
    struct sigaction action;
    action.sa_handler = SIG_IGN;
    sigemptyset (&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction (SIGPIPE, &action, NULL);
    sigaction (SIGHUP, &action, NULL);

    /* When lightdm starts sessions it needs to run itself in a new mode */
    if (argc >= 2 && strcmp (argv[1], "--session-child") == 0)
        return session_child_run (argc, argv);

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif
    loop = g_main_loop_new (NULL, FALSE);

    GList *messages = g_list_append (NULL, g_strdup_printf ("Starting Light Display Manager %s, UID=%i PID=%i", VERSION, getuid (), getpid ()));

    g_signal_connect (process_get_current (), PROCESS_SIGNAL_GOT_SIGNAL, G_CALLBACK (signal_cb), NULL);

    g_autoptr(GOptionContext) option_context = g_option_context_new (/* Arguments and description for --help test */
                                                                     _("- Display Manager"));
    gboolean test_mode = FALSE;
    gchar *pid_path = "/var/run/lightdm.pid";
    gchar *log_dir = NULL;
    gchar *run_dir = NULL;
    gchar *cache_dir = NULL;
    gboolean show_config = FALSE, show_version = FALSE;
    GOptionEntry options[] =
    {
        { "config", 'c', 0, G_OPTION_ARG_STRING, &config_path,
          /* Help string for command line --config flag */
          N_("Use configuration file"), "FILE" },
        { "debug", 'd', 0, G_OPTION_ARG_NONE, &debug,
          /* Help string for command line --debug flag */
          N_("Print debugging messages"), NULL },
        { "test-mode", 0, 0, G_OPTION_ARG_NONE, &test_mode,
          /* Help string for command line --test-mode flag */
          N_("Run as unprivileged user, skipping things that require root access"), NULL },
        { "pid-file", 0, 0, G_OPTION_ARG_STRING, &pid_path,
          /* Help string for command line --pid-file flag */
          N_("File to write PID into"), "FILE" },
        { "log-dir", 0, 0, G_OPTION_ARG_STRING, &log_dir,
          /* Help string for command line --log-dir flag */
          N_("Directory to write logs to"), "DIRECTORY" },
        { "run-dir", 0, 0, G_OPTION_ARG_STRING, &run_dir,
          /* Help string for command line --run-dir flag */
          N_("Directory to store running state"), "DIRECTORY" },
        { "cache-dir", 0, 0, G_OPTION_ARG_STRING, &cache_dir,
          /* Help string for command line --cache-dir flag */
          N_("Directory to cache information"), "DIRECTORY" },
        { "show-config", 0, 0, G_OPTION_ARG_NONE, &show_config,
          /* Help string for command line --show-config flag */
          N_("Show combined configuration"), NULL },
        { "version", 'v', 0, G_OPTION_ARG_NONE, &show_version,
          /* Help string for command line --version flag */
          N_("Show release version"), NULL },
        { NULL }
    };
    g_option_context_add_main_entries (option_context, options, GETTEXT_PACKAGE);
    g_autoptr(GError) error = NULL;
    gboolean result = g_option_context_parse (option_context, &argc, &argv, &error);
    if (error)
        g_printerr ("%s\n", error->message);
    if (!result)
    {
        g_printerr (/* Text printed out when an unknown command-line argument provided */
                    _("Run '%s --help' to see a full list of available command line options."), argv[0]);
        g_printerr ("\n");
        return EXIT_FAILURE;
    }

    /* Show combined configuration if user requested it */
    if (show_config)
    {
        if (!config_load_from_standard_locations (config_get_instance (), config_path, NULL))
            return EXIT_FAILURE;

        /* Number sources */
        GList *sources = config_get_sources (config_get_instance ());
        g_autoptr(GHashTable) source_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
        const gchar *last_source = "";
        int i = 0;
        for (GList *link = sources; link; i++, link = link->next)
        {
            const gchar *path = link->data;
            gchar *id;
            if (i < 26)
                id = g_strdup_printf ("%c", 'A' + i);
            else
                id = g_strdup_printf ("%d", i);
            g_hash_table_insert (source_ids, g_strdup (path), id);
            last_source = id;
        }
        g_autofree gchar *empty_source = g_strdup (last_source);
        for (i = 0; empty_source[i] != '\0'; i++)
            empty_source[i] = ' ';

        /* Print out keys */
        g_auto(GStrv) groups = config_get_groups (config_get_instance ());
        for (i = 0; groups[i]; i++)
        {
            if (i != 0)
                g_printerr ("\n");
            g_printerr ("%s  [%s]\n", empty_source, groups[i]);

            g_auto(GStrv) keys = config_get_keys (config_get_instance (), groups[i]);
            for (int j = 0; keys && keys[j]; j++)
            {
                const gchar *source, *id;
                g_autofree gchar *value = NULL;

                source = config_get_source (config_get_instance (), groups[i], keys[j]);
                id = source ? g_hash_table_lookup (source_ids, source) : empty_source;
                value = config_get_string (config_get_instance (), groups[i], keys[j]);
                g_printerr ("%s  %s=%s\n", id, keys[j], value);
            }

        }

        /* Show mapping from source number to path */
        g_printerr ("\n");
        g_printerr ("Sources:\n");
        for (GList *link = sources; link; link = link->next)
        {
            const gchar *path = link->data;
            const gchar *source = g_hash_table_lookup (source_ids, path);
            g_printerr ("%s  %s\n", source, path);
        }

        return EXIT_SUCCESS;
    }

    if (show_version)
    {
        /* NOTE: Is not translated so can be easily parsed */
        g_printerr ("lightdm %s\n", VERSION);
        return EXIT_SUCCESS;
    }

    if (!test_mode && getuid () != 0)
    {
        g_printerr ("Only root can run Light Display Manager.  To run as a regular user for testing run with the --test-mode flag.\n");
        return EXIT_FAILURE;
    }

    /* If running inside an X server use Xephyr for display */
    if (getenv ("DISPLAY") && getuid () != 0)
    {
        g_autofree gchar *x_server_path = g_find_program_in_path ("Xephyr");
        if (!x_server_path)
        {
            g_printerr ("Running inside an X server requires Xephyr to be installed but it cannot be found.  Please install it or update your PATH environment variable.\n");
            return EXIT_FAILURE;
        }
    }

    /* Make sure the system binary directory (where the greeters are installed) is in the path */
    if (test_mode)
    {
        const gchar *path = g_getenv ("PATH");
        g_autofree gchar *new_path = NULL;
        if (path)
            new_path = g_strdup_printf ("%s:%s", path, SBIN_DIR);
        else
            new_path = g_strdup (SBIN_DIR);
        g_setenv ("PATH", new_path, TRUE);
    }

    /* Write PID file */
    FILE *pid_file = fopen (pid_path, "w");
    if (pid_file)
    {
        fprintf (pid_file, "%d\n", getpid ());
        fclose (pid_file);
    }

    /* If not running as root write output to directories we control */
    g_autofree gchar *default_log_dir = NULL;
    g_autofree gchar *default_run_dir = NULL;
    g_autofree gchar *default_cache_dir = NULL;
    if (getuid () != 0)
    {
        default_log_dir = g_build_filename (g_get_user_cache_dir (), "lightdm", "log", NULL);
        default_run_dir = g_build_filename (g_get_user_cache_dir (), "lightdm", "run", NULL);
        default_cache_dir = g_build_filename (g_get_user_cache_dir (), "lightdm", "cache", NULL);
    }
    else
    {
        default_log_dir = g_strdup (LOG_DIR);
        default_run_dir = g_strdup (RUN_DIR);
        default_cache_dir = g_strdup (CACHE_DIR);
    }

    /* Load config file(s) */
    if (!config_load_from_standard_locations (config_get_instance (), config_path, &messages))
        exit (EXIT_FAILURE);
    g_free (config_path);

    /* Set default values */
    if (!config_has_key (config_get_instance (), "LightDM", "start-default-seat"))
        config_set_boolean (config_get_instance (), "LightDM", "start-default-seat", TRUE);
    if (!config_has_key (config_get_instance (), "LightDM", "minimum-vt"))
        config_set_integer (config_get_instance (), "LightDM", "minimum-vt", 7);
    if (!config_has_key (config_get_instance (), "LightDM", "guest-account-script"))
        config_set_string (config_get_instance (), "LightDM", "guest-account-script", "guest-account");
    if (!config_has_key (config_get_instance (), "LightDM", "greeter-user"))
        config_set_string (config_get_instance (), "LightDM", "greeter-user", GREETER_USER);
    if (!config_has_key (config_get_instance (), "LightDM", "lock-memory"))
        config_set_boolean (config_get_instance (), "LightDM", "lock-memory", TRUE);
    if (!config_has_key (config_get_instance (), "LightDM", "backup-logs"))
        config_set_boolean (config_get_instance (), "LightDM", "backup-logs", TRUE);
    if (!config_has_key (config_get_instance (), "LightDM", "dbus-service"))
        config_set_boolean (config_get_instance (), "LightDM", "dbus-service", TRUE);
    if (!config_has_key (config_get_instance (), "Seat:*", "type"))
        config_set_string (config_get_instance (), "Seat:*", "type", "local");
    if (!config_has_key (config_get_instance (), "Seat:*", "pam-service"))
        config_set_string (config_get_instance (), "Seat:*", "pam-service", "lightdm");
    if (!config_has_key (config_get_instance (), "Seat:*", "pam-autologin-service"))
        config_set_string (config_get_instance (), "Seat:*", "pam-autologin-service", "lightdm-autologin");
    if (!config_has_key (config_get_instance (), "Seat:*", "pam-greeter-service"))
        config_set_string (config_get_instance (), "Seat:*", "pam-greeter-service", "lightdm-greeter");
    if (!config_has_key (config_get_instance (), "Seat:*", "xserver-command"))
        config_set_string (config_get_instance (), "Seat:*", "xserver-command", "X");
    if (!config_has_key (config_get_instance (), "Seat:*", "xmir-command"))
        config_set_string (config_get_instance (), "Seat:*", "xmir-command", "Xmir");
    if (!config_has_key (config_get_instance (), "Seat:*", "xserver-share"))
        config_set_boolean (config_get_instance (), "Seat:*", "xserver-share", TRUE);
    if (!config_has_key (config_get_instance (), "Seat:*", "start-session"))
        config_set_boolean (config_get_instance (), "Seat:*", "start-session", TRUE);
    if (!config_has_key (config_get_instance (), "Seat:*", "allow-user-switching"))
        config_set_boolean (config_get_instance (), "Seat:*", "allow-user-switching", TRUE);
    if (!config_has_key (config_get_instance (), "Seat:*", "allow-guest"))
        config_set_boolean (config_get_instance (), "Seat:*", "allow-guest", TRUE);
    if (!config_has_key (config_get_instance (), "Seat:*", "greeter-allow-guest"))
        config_set_boolean (config_get_instance (), "Seat:*", "greeter-allow-guest", TRUE);
    if (!config_has_key (config_get_instance (), "Seat:*", "greeter-show-remote-login"))
        config_set_boolean (config_get_instance (), "Seat:*", "greeter-show-remote-login", TRUE);
    if (!config_has_key (config_get_instance (), "Seat:*", "greeter-session"))
        config_set_string (config_get_instance (), "Seat:*", "greeter-session", DEFAULT_GREETER_SESSION);
    if (!config_has_key (config_get_instance (), "Seat:*", "user-session"))
        config_set_string (config_get_instance (), "Seat:*", "user-session", DEFAULT_USER_SESSION);
    if (!config_has_key (config_get_instance (), "Seat:*", "session-wrapper"))
        config_set_string (config_get_instance (), "Seat:*", "session-wrapper", "lightdm-session");
    if (!config_has_key (config_get_instance (), "LightDM", "log-directory"))
        config_set_string (config_get_instance (), "LightDM", "log-directory", default_log_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "run-directory"))
        config_set_string (config_get_instance (), "LightDM", "run-directory", default_run_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "cache-directory"))
        config_set_string (config_get_instance (), "LightDM", "cache-directory", default_cache_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "sessions-directory"))
        config_set_string (config_get_instance (), "LightDM", "sessions-directory", SESSIONS_DIR);
    if (!config_has_key (config_get_instance (), "LightDM", "remote-sessions-directory"))
        config_set_string (config_get_instance (), "LightDM", "remote-sessions-directory", REMOTE_SESSIONS_DIR);
    if (!config_has_key (config_get_instance (), "LightDM", "greeters-directory"))
    {
        g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);
        const gchar * const *data_dirs = g_get_system_data_dirs ();
        for (int i = 0; data_dirs[i]; i++)
            g_ptr_array_add (dirs, g_build_filename (data_dirs[i], "lightdm/greeters", NULL));
        for (int i = 0; data_dirs[i]; i++)
            g_ptr_array_add (dirs, g_build_filename (data_dirs[i], "xgreeters", NULL));
        g_ptr_array_add (dirs, NULL);
        g_autofree gchar *value = g_strjoinv (":", (gchar **) dirs->pdata);
        config_set_string (config_get_instance (), "LightDM", "greeters-directory", value);
    }
    if (!config_has_key (config_get_instance (), "XDMCPServer", "hostname"))
        config_set_string (config_get_instance (), "XDMCPServer", "hostname", g_get_host_name ());
    if (!config_has_key (config_get_instance (), "LightDM", "logind-check-graphical"))
        config_set_string (config_get_instance (), "LightDM", "logind-check-graphical", TRUE);

    /* Override defaults */
    if (log_dir)
        config_set_string (config_get_instance (), "LightDM", "log-directory", log_dir);
    g_free (log_dir);
    if (run_dir)
        config_set_string (config_get_instance (), "LightDM", "run-directory", run_dir);
    g_free (run_dir);
    if (cache_dir)
        config_set_string (config_get_instance (), "LightDM", "cache-directory", cache_dir);
    g_free (cache_dir);

    /* Create run and cache directories */
    g_autofree gchar *log_dir_path = config_get_string (config_get_instance (), "LightDM", "log-directory");
    if (g_mkdir_with_parents (log_dir_path, S_IRWXU | S_IXGRP | S_IXOTH) < 0)
        g_warning ("Failed to make log directory %s: %s", log_dir_path, strerror (errno));
    g_autofree gchar *run_dir_path = config_get_string (config_get_instance (), "LightDM", "run-directory");
    if (g_mkdir_with_parents (run_dir_path, S_IRWXU | S_IXGRP | S_IXOTH) < 0)
        g_warning ("Failed to make run directory %s: %s", run_dir_path, strerror (errno));
    g_autofree gchar *cache_dir_path = config_get_string (config_get_instance (), "LightDM", "cache-directory");
    if (g_mkdir_with_parents (cache_dir_path, S_IRWXU | S_IXGRP | S_IXOTH) < 0)
        g_warning ("Failed to make cache directory %s: %s", cache_dir_path, strerror (errno));

    log_init ();

    /* Show queued messages once logging is complete */
    for (GList *link = messages; link; link = link->next)
        g_debug ("%s", (gchar *)link->data);
    g_list_free_full (messages, g_free);

    if (getuid () != 0)
        g_debug ("Running in user mode");
    if (getenv ("DISPLAY"))
        g_debug ("Using Xephyr for X servers");

    display_manager = display_manager_new ();
    g_signal_connect (display_manager, DISPLAY_MANAGER_SIGNAL_STOPPED, G_CALLBACK (display_manager_stopped_cb), NULL);
    g_signal_connect (display_manager, DISPLAY_MANAGER_SIGNAL_SEAT_REMOVED, G_CALLBACK (display_manager_seat_removed_cb), NULL);

    if (config_get_boolean (config_get_instance (), "LightDM", "dbus-service"))
    {
        display_manager_service = display_manager_service_new (display_manager);
        g_signal_connect (display_manager_service, DISPLAY_MANAGER_SERVICE_SIGNAL_ADD_XLOCAL_SEAT, G_CALLBACK (service_add_xlocal_seat_cb), NULL);
        g_signal_connect (display_manager_service, DISPLAY_MANAGER_SERVICE_SIGNAL_READY, G_CALLBACK (service_ready_cb), NULL);
        g_signal_connect (display_manager_service, DISPLAY_MANAGER_SERVICE_SIGNAL_NAME_LOST, G_CALLBACK (service_name_lost_cb), NULL);
        display_manager_service_start (display_manager_service);
    }
    else
        start_display_manager ();

    shared_data_manager_start (shared_data_manager_get_instance ());

    /* Connect to logind */
    if (login1_service_connect (login1_service_get_instance ()))
    {
        /* Load dynamic seats from logind */
        g_debug ("Monitoring logind for seats");

        if (config_get_boolean (config_get_instance (), "LightDM", "start-default-seat"))
        {
            g_signal_connect (login1_service_get_instance (), LOGIN1_SERVICE_SIGNAL_SEAT_ADDED, G_CALLBACK (login1_service_seat_added_cb), NULL);
            g_signal_connect (login1_service_get_instance (), LOGIN1_SERVICE_SIGNAL_SEAT_REMOVED, G_CALLBACK (login1_service_seat_removed_cb), NULL);

            for (GList *link = login1_service_get_seats (login1_service_get_instance ()); link; link = link->next)
            {
                Login1Seat *login1_seat = link->data;
                if (!login1_add_seat (login1_seat))
                    return EXIT_FAILURE;
            }
        }
    }
    else
    {
        if (config_get_boolean (config_get_instance (), "LightDM", "start-default-seat"))
        {
            g_debug ("Adding default seat");

            g_auto(GStrv) types = config_get_string_list (config_get_instance (), "Seat:*", "type");
            g_autoptr(Seat) seat = NULL;
            for (gchar **type = types; type && *type; type++)
            {
                seat = create_seat (*type, "seat0");
                if (seat)
                    break;
            }
            if (seat)
            {
                set_seat_properties (seat, NULL);
                seat_set_property (seat, "exit-on-failure", "true");
                if (!display_manager_add_seat (display_manager, seat))
                    return EXIT_FAILURE;
            }
            else
            {
                g_warning ("Failed to create default seat");
                return EXIT_FAILURE;
            }
        }
    }

    g_main_loop_run (loop);

    /* Clean up shared data manager */
    shared_data_manager_cleanup ();

    /* Clean up user list */
    common_user_list_cleanup ();

    /* Remove D-Bus interface */
    g_clear_object (&display_manager_service);

    /* Clean up display manager */
    g_clear_object (&display_manager);

    g_debug ("Exiting with return value %d", exit_code);
    return exit_code;
}
