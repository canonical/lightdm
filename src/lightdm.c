/*
 * Copyright (C) 2010 Robert Ancell.
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
#include <signal.h>
#include <unistd.h>
#include <dbus/dbus-glib-bindings.h>

#include "display-manager.h"
#include "user-manager.h"
#include "xserver.h"

static DBusGConnection *bus = NULL;
static GKeyFile *config_file = NULL;
static const gchar *config_path = CONFIG_FILE;
static const gchar *pid_path = "/var/run/lightdm.pid";
static GMainLoop *loop = NULL;
static gboolean test_mode = FALSE;
static GTimer *log_timer;
static FILE *log_file;
static gboolean debug = FALSE;
static int signal_pipe[2];

static DisplayManager *display_manager = NULL;

#define LDM_BUS_NAME "org.lightdm.LightDisplayManager"

static gboolean
handle_signal (GIOChannel *source, GIOCondition condition, gpointer data)
{
    int signo;
    pid_t pid;

    if (read (signal_pipe[0], &signo, sizeof (int)) < 0 || 
        read (signal_pipe[0], &pid, sizeof (pid_t)) < 0)
        return TRUE;

    if (signo == SIGUSR1)
        xserver_handle_signal (pid);
    else
    {
        g_debug ("Caught %s signal, exiting", g_strsignal (signo));
        g_object_unref (display_manager);
        g_main_loop_quit (loop);
    }

    return TRUE;
}

static void
signal_cb (int signum, siginfo_t *info, void *data)
{
    if (write (signal_pipe[1], &info->si_signo, sizeof (int)) < 0 ||
        write (signal_pipe[1], &info->si_pid, sizeof (pid_t)) < 0)
        g_warning ("Failed to write to signal pipe");
}

static void
handle_signals (void)
{
    struct sigaction action;

    /* Catch signals and feed them to the main loop via a pipe */
    if (pipe (signal_pipe) != 0)
        g_critical ("Failed to create signal pipe");
    g_io_add_watch (g_io_channel_unix_new (signal_pipe[0]), G_IO_IN, handle_signal, NULL);
    action.sa_sigaction = signal_cb;
    sigemptyset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGHUP, &action, NULL);
    sigaction (SIGUSR1, &action, NULL);
}

static void
version (void)
{
    /* NOTE: Is not translated so can be easily parsed */
    g_printerr ("%s %s\n", LIGHTDM_BINARY, VERSION);
}

static void
usage (void)
{
    g_printerr (/* Description on how to use Light Display Manager displayed on command-line */
                _("Usage:\n"
                  "  %s - Display Manager"), LIGHTDM_BINARY);

    g_printerr ("\n\n");

    g_printerr (/* Description on how to use Light Display Manager displayed on command-line */    
                _("Help Options:\n"
                  "  -c, --config <file>             Use configuration file\n"
                  "      --pid-file <file>           File to write PID into\n"
                  "  -d, --debug                     Print debugging messages\n"
                  "      --test-mode                 Run as unprivileged user\n"
                  "  -v, --version                   Show release version\n"
                  "  -h, --help                      Show help options"));
    g_printerr ("\n\n");
}

static void
get_options (int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++)
    {
        char *arg = argv[i];

        if (strcmp (arg, "-c") == 0 ||
            strcmp (arg, "--config") == 0) {
            i++;
            if (i == argc)
            {
               usage ();
               exit (1);
            }
            config_path = argv[i];
        }
        else if (strcmp (arg, "--pid-file") == 0)
        {
            i++;
            if (i == argc)
            {
               usage ();
               exit (1);
            }
            pid_path = argv[i];
        }
        else if (strcmp (arg, "-d") == 0 ||
            strcmp (arg, "--debug") == 0) {
            debug = TRUE;
        }
        else if (strcmp (arg, "--test-mode") == 0) {
            test_mode = TRUE;
        }
        else if (strcmp (arg, "-v") == 0 ||
            strcmp (arg, "--version") == 0)
        {
            version ();
            exit (0);
        }
        else if (strcmp (arg, "-h") == 0 ||
                 strcmp (arg, "--help") == 0)
        {
            usage ();
            exit (0);
        }
        else
        {
            g_printerr ("Unknown argument: '%s'\n", arg);
            exit (1);
        }      
    }
}

static DBusGConnection *
start_dbus (void)
{
    DBusGConnection *bus;
    DBusGProxy *proxy;
    guint result;
    GError *error = NULL;

    bus = dbus_g_bus_get (test_mode ? DBUS_BUS_SESSION : DBUS_BUS_SYSTEM, &error);
    if (!bus)
    {
        if (test_mode)
            g_critical ("Failed to get session bus: %s", error->message);
        else
            g_critical ("Failed to get system bus: %s", error->message);
        return NULL;
    }
    g_clear_error (&error);

    proxy = dbus_g_proxy_new_for_name (bus,
                                       DBUS_SERVICE_DBUS,
                                       DBUS_PATH_DBUS,
                                       DBUS_INTERFACE_DBUS);
    if (!org_freedesktop_DBus_request_name (proxy,
                                            LDM_BUS_NAME,
                                            DBUS_NAME_FLAG_DO_NOT_QUEUE, &result, &error))
    {
        if (g_error_matches (error, DBUS_GERROR, DBUS_GERROR_ACCESS_DENIED))
           g_printerr ("Not authorised to use bus name " LDM_BUS_NAME ", do you have appropriate permissions?\n");
        else
           g_printerr ("Failed to register D-Bus name: %s\n", error->message);
        exit (1);
    }
    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) 
    {
        g_printerr ("Light Display Manager already running\n");
        exit (1);
    }
    g_object_unref (proxy);

    return bus;
}

static void
load_config (void)
{
    GError *error = NULL;

    config_file = g_key_file_new ();
    if (!g_key_file_load_from_file (config_file, config_path, G_KEY_FILE_NONE, &error))
        g_warning ("Failed to load configuration from %s: %s", config_path, error->message); // FIXME: Don't make warning on no file, just info
    g_clear_error (&error);

    if (test_mode)
    {
        gchar *path;
        g_key_file_set_boolean (config_file, "LightDM", "test-mode", TRUE);
        path = g_build_filename (g_get_user_cache_dir (), "lightdm", "authority", NULL);
        g_key_file_set_string (config_file, "LightDM", "authorization-directory", path);
        g_free (path);
        path = g_build_filename (g_get_user_cache_dir (), "lightdm", NULL);
        g_key_file_set_string (config_file, "LightDM", "log-directory", path);
        g_free (path);
    }
}

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
    log_dir = g_key_file_get_string (config_file, "LightDM", "log-directory", NULL);
    if (!log_dir)
        log_dir = g_strdup (LOG_DIR);
    g_mkdir_with_parents (log_dir, 0700);
    path = g_build_filename (log_dir, "lightdm.log", NULL);
    g_free (log_dir);

    log_file = fopen (path, "w");
    g_free (path);
    g_log_set_default_handler (log_cb, NULL);
}

static void
display_added_cb (DisplayManager *manager, Display *display)
{
    gchar *name;
    name = g_strdup_printf ("/org/lightdm/LightDisplayManager/Display%d", display_get_index (display));
    dbus_g_connection_register_g_object (bus, name, G_OBJECT (display));
    g_free (name);
}

int
main(int argc, char **argv)
{
    FILE *pid_file;
    UserManager *user_manager;

    handle_signals ();

    g_thread_init (NULL);
    g_type_init ();

    get_options (argc, argv);

    /* Write PID file */
    pid_file = fopen (pid_path, "w");
    if (pid_file)
    {
        fprintf (pid_file, "%d\n", getpid ());
        fclose (pid_file);
    }

    /* Check if root */
    if (!test_mode && getuid () != 0)
    {
        g_printerr ("Only root can run Light Display Manager\n");
        return 1;
    }

    bus = start_dbus ();
    if (!bus)
        return 1;

    // Change working directory?

    loop = g_main_loop_new (NULL, FALSE);

    load_config ();

    log_init ();

    g_debug ("Starting Light Display Manager %s, PID=%i", VERSION, getpid ());

    g_debug ("Loaded configuration from %s", config_path);

    user_manager = user_manager_new (config_file);
    dbus_g_connection_register_g_object (bus, "/org/lightdm/LightDisplayManager/Users", G_OBJECT (user_manager));

    display_manager = display_manager_new (config_file);
    g_signal_connect (display_manager, "display-added", G_CALLBACK (display_added_cb), NULL);
    dbus_g_connection_register_g_object (bus, "/org/lightdm/LightDisplayManager", G_OBJECT (display_manager));

    display_manager_start (display_manager);

    g_main_loop_run (loop);

    return 0;
}
