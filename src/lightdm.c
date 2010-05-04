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

#include "display-manager.h"
#include "user-manager.h"
#include "session-manager.h"

static GKeyFile *config_file;
static const gchar *config_path = CONFIG_FILE;
static GMainLoop *loop;
static gboolean debug = FALSE;

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

static void
signal_handler (int signum)
{
    g_debug ("Caught %s signal, exiting", g_strsignal (signum));
    g_main_loop_quit (loop);
}

int
main(int argc, char **argv)
{
    DisplayManager *display_manager;
    UserManager *user_manager;
    SessionManager *session_manager;
    DBusGConnection *bus;
    struct sigaction action;
    gchar *default_user;
    GError *error = NULL;

    /* Quit cleanly on signals */
    action.sa_handler = signal_handler;
    sigemptyset (&action.sa_mask);
    action.sa_flags = 0;
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGHUP, &action, NULL);

    g_type_init ();

    if (getuid () != 0)
    {
        g_print ("Only root can run Light Display Manager\n");
        return -1;
    }

    get_options (argc, argv);

    g_debug ("Starting Light Display Manager %s, PID=%i", VERSION, getpid ());

    // Change working directory?

    loop = g_main_loop_new (NULL, FALSE);

    config_file = g_key_file_new ();
    if (g_key_file_load_from_file(config_file, config_path, G_KEY_FILE_NONE, &error))
        g_debug ("Loaded configuration from %s", config_path);
    else
        g_warning ("Failed to load configuration from %s: %s", config_path, error->message); // FIXME: Don't make warning on no file, just info
    g_clear_error (&error);

    bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
    if (!bus) 
        g_critical ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);

    user_manager = user_manager_new ();
    dbus_g_connection_register_g_object (bus, "/org/gnome/LightDisplayManager/Users", G_OBJECT (user_manager));

    session_manager = session_manager_new ();
    dbus_g_connection_register_g_object (bus, "/org/gnome/LightDisplayManager/Session", G_OBJECT (session_manager));

    display_manager = display_manager_new ();
    dbus_g_connection_register_g_object (bus, "/org/gnome/LightDisplayManager", G_OBJECT (display_manager));

    /* Automatically log in or start a greeter session */  
    default_user = g_key_file_get_value (config_file, "Default User", "name", &error);
    if (default_user)
        g_debug ("Starting session for default user %s", default_user);
    display_manager_add_display (display_manager, default_user);    

    g_main_loop_run (loop);

    return 0;
}
