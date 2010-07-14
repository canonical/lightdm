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

#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <signal.h>
#include <unistd.h>
#include <dbus/dbus-glib-bindings.h>

#include "display-manager.h"
#include "user-manager.h"
#include "xserver.h"
#include "xdmcp-server.h"

static DBusGConnection *bus = NULL;
static GKeyFile *config_file = NULL;
static const gchar *config_path = CONFIG_FILE;
static DBusBusType bus_type = DBUS_BUS_SYSTEM;
static GMainLoop *loop = NULL;
static gboolean do_root_check = TRUE;
static gboolean debug = FALSE;

static DisplayManager *display_manager = NULL;

#define LDM_BUS_NAME "org.gnome.LightDisplayManager"

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
                  "  -d, --debug                     Print debugging messages\n"
                  "      --no-root-check             Don't check if root user\n"
                  "      --use-session-bus           Use the session D-Bus\n"
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
        else if (strcmp (arg, "-d") == 0 ||
            strcmp (arg, "--debug") == 0) {
            debug = TRUE;
        }
        else if (strcmp (arg, "--no-root-check") == 0) {
            do_root_check = FALSE;
        }
        else if (strcmp (arg, "--use-session-bus") == 0) {
            bus_type = DBUS_BUS_SESSION;
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

static gboolean
handle_signal (gpointer data)
{
    siginfo_t *info = data;

    if (info->si_signo == SIGUSR1)
    {
        xserver_handle_signal (info->si_pid);
        return FALSE;
    }

    g_debug ("Caught %s signal, exiting", g_strsignal (info->si_signo));
    g_main_loop_quit (loop);

    return FALSE;
}

static void
signal_cb (int signum, siginfo_t *info, void *data)
{
    g_idle_add (handle_signal, info);
}

static DBusGConnection *
start_dbus (void)
{
    DBusGConnection *bus;
    DBusGProxy *proxy;
    guint result;
    GError *error = NULL;

    bus = dbus_g_bus_get (bus_type, &error);
    if (!bus) 
        g_critical ("Failed to get system bus: %s", error->message);
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
display_added_cb (DisplayManager *manager, Display *display)
{
    gchar *name;
    name = g_strdup_printf ("/org/gnome/LightDisplayManager/Display%d", display_get_index (display));
    dbus_g_connection_register_g_object (bus, name, G_OBJECT (display));
    g_free (name);
}

#include "theme.h"
#include "session.h"
static void
xdmcp_session_cb (XDMCPServer *server, XDMCPSession *session)
{
    Display *display;
    gchar *default_session;

    default_session = g_key_file_get_value (config_file, "LightDM", "session", NULL);
    if (!default_session)
        default_session = g_strdup (DEFAULT_SESSION);

    display = display_manager_add_display (display_manager);
    display_set_remote_host (display, "localhost", xdmcp_session_get_display_number (session));
    display_start (display, default_session, NULL, 0);

    g_free (default_session);
}

int
main(int argc, char **argv)
{
    UserManager *user_manager;
    struct sigaction action;
    gchar *default_user, *default_session;
    gint user_timeout;
    GError *error = NULL;

    /* Quit cleanly on signals */
    action.sa_sigaction = signal_cb;
    sigemptyset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGHUP, &action, NULL);
    sigaction (SIGUSR1, &action, NULL);

    g_type_init ();

    get_options (argc, argv);

    if (do_root_check && getuid () != 0)
    {
        g_printerr ("Only root can run Light Display Manager\n");
        return -1;
    }

    bus = start_dbus ();

    g_debug ("Starting Light Display Manager %s, PID=%i", VERSION, getpid ());

    // Change working directory?

    loop = g_main_loop_new (NULL, FALSE);

    config_file = g_key_file_new ();
    if (g_key_file_load_from_file(config_file, config_path, G_KEY_FILE_NONE, &error))
        g_debug ("Loaded configuration from %s", config_path);
    else
        g_warning ("Failed to load configuration from %s: %s", config_path, error->message); // FIXME: Don't make warning on no file, just info
    g_clear_error (&error);

    user_manager = user_manager_new ();
    dbus_g_connection_register_g_object (bus, "/org/gnome/LightDisplayManager/Users", G_OBJECT (user_manager));

    display_manager = display_manager_new (config_file);
    g_signal_connect (display_manager, "display-added", G_CALLBACK (display_added_cb), NULL);
    dbus_g_connection_register_g_object (bus, "/org/gnome/LightDisplayManager", G_OBJECT (display_manager));

    /* Start the first display */
    if (!g_key_file_get_boolean (config_file, "LightDM", "headless", NULL))
    {
        Display *display;

        display = display_manager_add_display (display_manager);

        default_session = g_key_file_get_value (config_file, "LightDM", "session", NULL);
        if (!default_session)
            default_session = g_strdup (DEFAULT_SESSION);

        /* Automatically log in or start a greeter session */  
        default_user = g_key_file_get_value (config_file, "Default User", "name", NULL);
        //FIXME default_user_session = g_key_file_get_value (config_file, "Default User", "session", NULL); // FIXME
        user_timeout = g_key_file_get_integer (config_file, "Default User", "timeout", NULL);
        if (user_timeout < 0)
            user_timeout = 0;

        if (default_user)
        {
            if (user_timeout == 0)
                g_debug ("Starting session for user %s", default_user);
            else
                g_debug ("Starting session for user %s in %d seconds", default_user, user_timeout);
        }

        display_start (display, default_session, default_user, user_timeout);
        g_free (default_session);
        g_free (default_user);
    }

    if (g_key_file_get_boolean (config_file, "xdmcp", "enabled", NULL))
    {
        XDMCPServer *xdmcp_server;

        xdmcp_server = xdmcp_server_new (config_file);
        g_signal_connect (xdmcp_server, "session-added", G_CALLBACK (xdmcp_session_cb), NULL);
        xdmcp_server_start (xdmcp_server);
    }

    g_main_loop_run (loop);

    return 0;
}
