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
#include <unistd.h>

#include "display-manager.h"
#include "user-manager.h"
#include "xserver.h"

static GKeyFile *config_file = NULL;
static const gchar *config_path = CONFIG_FILE;
static const gchar *pid_path = "/var/run/lightdm.pid";
static GMainLoop *loop = NULL;
static gboolean test_mode = FALSE;
static GTimer *log_timer;
static FILE *log_file;
static gboolean debug = FALSE;

static UserManager *user_manager = NULL;
static DisplayManager *display_manager = NULL;

#define LDM_BUS_NAME "org.lightdm.LightDisplayManager"

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
    g_debug ("Caught %s signal, exiting", g_strsignal (signum));
    g_object_unref (display_manager);
    g_main_loop_quit (loop);
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
    if (g_strcmp0 (method_name, "AddDisplay") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("()")))
            return;

        display_manager_add_display (display_manager);
        g_dbus_method_invocation_return_value (invocation, NULL);
    }
    else if (g_strcmp0 (method_name, "SwitchToUser") == 0)
    {
        gchar *username;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
            return;

        g_variant_get (parameters, "(s)", &username);
        display_manager_switch_to_user (display_manager, username);
        g_dbus_method_invocation_return_value (invocation, NULL);
        g_free (username);
    }
}

static void
handle_user_manager_call (GDBusConnection       *connection,
                          const gchar           *sender,
                          const gchar           *object_path,
                          const gchar           *interface_name,
                          const gchar           *method_name,
                          GVariant              *parameters,
                          GDBusMethodInvocation *invocation,
                          gpointer               user_data)
{
    if (g_strcmp0 (method_name, "GetUsers") == 0)
    {
        GVariantBuilder *builder;
        GVariant *arg0;
        GList *users, *iter;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("()")))
            return;

        builder = g_variant_builder_new (G_VARIANT_TYPE ("a(sssb)"));
        users = user_manager_get_users (user_manager);
        for (iter = users; iter; iter = iter->next)
        {
            UserInfo *info = iter->data;
            g_variant_builder_add_value (builder, g_variant_new ("(sssb)", info->name, info->real_name, info->image, info->logged_in));
        }
        arg0 = g_variant_builder_end (builder);
        g_dbus_method_invocation_return_value (invocation, g_variant_new_tuple (&arg0, 1));
        g_variant_builder_unref (builder);
    }
    else if (g_strcmp0 (method_name, "GetUserDefaults") == 0)
    {
        gchar *username, *language, *layout, *session;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
            return;

        g_variant_get (parameters, "(s)", &username);
        if (user_manager_get_user_defaults (user_manager, username, &language, &layout, &session))
        {
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(sss)", language, layout, session));
            g_free (language);
            g_free (layout);
            g_free (session);
        }
        g_free (username);
    }
}

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    const gchar *display_manager_interface =
        "<node>"
        "  <interface name='org.lightdm.LightDisplayManager'>"
        "    <method name='AddDisplay'/>"
        "    <method name='SwitchToUser'>"
        "      <arg name='username' direction='in' type='s'/>"
        "    </method>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable display_manager_vtable =
    {
        handle_display_manager_call
    };

    const gchar *user_manager_interface =
        "<node>"
        "  <interface name='org.lightdm.LightDisplayManager.Users'>"
        "    <method name='GetUsers'>"
        "      <arg name='users' direction='out' type='a(sssb)'/>"
        "    </method>"
        "    <method name='GetUserDefaults'>"
        "      <arg name='username' direction='in' type='s'/>"
        "      <arg name='language' direction='out' type='s'/>"
        "      <arg name='layout' direction='out' type='s'/>"
        "      <arg name='session' direction='out' type='s'/>"
        "    </method>"
        "  </interface>"
        "</node>";
    static const GDBusInterfaceVTable user_manager_vtable =
    {
        handle_user_manager_call
    };
    GDBusNodeInfo *display_manager_info, *user_manager_info;

    display_manager_info = g_dbus_node_info_new_for_xml (display_manager_interface, NULL);
    g_assert (display_manager_info != NULL);
    g_dbus_connection_register_object (connection,
                                       "/org/lightdm/LightDisplayManager",
                                       display_manager_info->interfaces[0],
                                       &display_manager_vtable,
                                       NULL, NULL,
                                       NULL);

    user_manager_info = g_dbus_node_info_new_for_xml (user_manager_interface, NULL);
    g_assert (user_manager_info != NULL);
    g_dbus_connection_register_object (connection,
                                       "/org/lightdm/LightDisplayManager/Users",
                                       user_manager_info->interfaces[0],
                                       &user_manager_vtable,
                                       NULL, NULL,
                                       NULL);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
    if (connection)
        g_printerr ("Failed to use bus name " LDM_BUS_NAME ", do you have appropriate permissions?\n");
    else
        g_printerr ("Failed to get system bus");

    exit (EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
    FILE *pid_file;

    g_thread_init (NULL);
    g_type_init ();

    g_signal_connect (child_process_get_parent (), "got-signal", G_CALLBACK (signal_cb), NULL);

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
        g_printerr ("Only root can run Light Display Manager.  To run as a regular user for testing run with the --test-mode flag.\n");
        return 1;
    }

    /* Test mode requires Xephry */
    if (test_mode)
    {
        gchar *xephyr_path;
      
        xephyr_path = g_find_program_in_path ("Xephyr");
        if (!xephyr_path)
        {
            g_printerr ("Test mode requires Xephyr to be installed but it cannot be found.  Please install it or update your PATH environment variable.\n");
            return 1;
        }
        g_free (xephyr_path);
    }

    loop = g_main_loop_new (NULL, FALSE);

    g_bus_own_name (test_mode ? G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM,
                    LDM_BUS_NAME,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    bus_acquired_cb,
                    NULL,
                    name_lost_cb,
                    NULL,
                    NULL);

    load_config ();

    log_init ();

    g_debug ("Starting Light Display Manager %s, PID=%i", VERSION, getpid ());

    g_debug ("Loaded configuration from %s", config_path);

    user_manager = user_manager_new (config_file);
    display_manager = display_manager_new (config_file);

    display_manager_start (display_manager);

    g_main_loop_run (loop);

    return 0;
}
