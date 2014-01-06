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
#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

static GBusType bus_type = G_BUS_TYPE_SYSTEM;
static GDBusProxy *dm_proxy, *seat_proxy = NULL;

static gint xephyr_display_number;
static GPid xephyr_pid;

static void
usage ()
{
    g_printerr (/* Text printed out when an unknown command-line argument provided */
                _("Run 'dm-tool --help' to see a full list of available command line options."));
    g_printerr ("\n");
}

static void
xephyr_setup_cb (gpointer user_data)
{
    signal (SIGUSR1, SIG_IGN);
}

static void
xephyr_signal_cb (int signum)
{
    gchar *path;
    GVariant *result;
    GError *error = NULL;

    result = g_dbus_proxy_call_sync (dm_proxy,
                                     "AddLocalXSeat",
                                     g_variant_new ("(i)", xephyr_display_number),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    if (!result)
    {
        g_printerr ("Unable to add seat: %s\n", error->message);
        kill (xephyr_pid, SIGQUIT);
        exit (EXIT_FAILURE);
    }

    if (!g_variant_is_of_type (result, G_VARIANT_TYPE ("(o)")))
    {
        g_printerr ("Unexpected response to AddSeat: %s\n", g_variant_get_type_string (result));
        exit (EXIT_FAILURE);
    }

    g_variant_get (result, "(&o)", &path);
    g_print ("%s\n", path);

    exit (EXIT_SUCCESS);
}

GDBusProxy *
get_seat_proxy (void)
{
    GError *error = NULL;

    if (seat_proxy)
        return seat_proxy;
  
    if (!g_getenv ("XDG_SEAT_PATH"))
    {
        g_printerr ("Not running inside a display manager, XDG_SEAT_PATH not defined\n");
        exit (EXIT_FAILURE);
    }

    seat_proxy = g_dbus_proxy_new_for_bus_sync (bus_type,
                                                G_DBUS_PROXY_FLAGS_NONE,
                                                NULL,
                                                "org.freedesktop.DisplayManager",
                                                g_getenv ("XDG_SEAT_PATH"),
                                                "org.freedesktop.DisplayManager.Seat",
                                                NULL,
                                                &error);
    if (!seat_proxy)
    {
        g_printerr ("Unable to contact display manager: %s\n", error->message);
        exit (EXIT_FAILURE);
    }
    g_clear_error (&error);
  
    return seat_proxy;
}

int
main (int argc, char **argv)
{
    gchar *command;
    gint n_options;
    gchar **options;
    GError *error = NULL;
    gint arg_index;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    for (arg_index = 1; arg_index < argc; arg_index++)
    {
        gchar *arg = argv[arg_index];

        if (!g_str_has_prefix (arg, "-"))
            break;
      
        if (strcmp (arg, "-h") == 0 || strcmp (arg, "--help") == 0)
        {
            g_printerr ("Usage:\n"
                        "  dm-tool [OPTION...] COMMAND [ARGS...] - Display Manager tool\n"
                        "\n"
                        "Options:\n"
                        "  -h, --help        Show help options\n"
                        "  -v, --version     Show release version\n"
                        "  --session-bus     Use session D-Bus\n"
                        "\n"
                        "Commands:\n"
                        "  switch-to-greeter                                    Switch to the greeter\n"
                        "  switch-to-user USERNAME [SESSION]                    Switch to a user session\n"
                        "  switch-to-guest [SESSION]                            Switch to a guest session\n"
                        "  lock                                                 Lock the current seat\n"
                        "  list-seats                                           List the active seats\n"
                        "  add-nested-seat [--fullscreen|--screen DIMENSIONS]   Start a nested display\n"
                        "  add-local-x-seat DISPLAY_NUMBER                      Add a local X seat\n"
                        "  add-seat TYPE [NAME=VALUE...]                        Add a dynamic seat\n");
            return EXIT_SUCCESS;
        }
        else if (strcmp (arg, "-v") == 0 || strcmp (arg, "--version") == 0)
        {
            /* NOTE: Is not translated so can be easily parsed */
            g_printerr ("lightdm %s\n", VERSION);
            return EXIT_SUCCESS;
        }
        else if (strcmp (arg, "--session-bus") == 0)
            bus_type = G_BUS_TYPE_SESSION;
        else
        {
            g_printerr ("Unknown option %s\n", arg);
            usage ();
            return EXIT_FAILURE;
        }
    }

    if (arg_index >= argc)
    {
        g_printerr ("Missing command\n");
        usage ();
        return EXIT_FAILURE;
    }

    dm_proxy = g_dbus_proxy_new_for_bus_sync (bus_type,
                                              G_DBUS_PROXY_FLAGS_NONE,
                                              NULL,
                                              "org.freedesktop.DisplayManager",
                                              "/org/freedesktop/DisplayManager",
                                              "org.freedesktop.DisplayManager",
                                              NULL,
                                              &error);
    if (!dm_proxy)
    {
        g_printerr ("Unable to contact display manager: %s\n", error->message);
        return EXIT_FAILURE;
    }
    g_clear_error (&error);
  
    command = argv[arg_index];
    arg_index++;
    n_options = argc - arg_index;
    options = argv + arg_index;
    if (strcmp (command, "switch-to-greeter") == 0)
    {
        if (n_options != 0)
        {
            g_printerr ("Usage switch-to-greeter\n");
            usage ();
            return EXIT_FAILURE;
        }

        if (!g_dbus_proxy_call_sync (get_seat_proxy (),
                                     "SwitchToGreeter",
                                     g_variant_new ("()"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error))
        {
            g_printerr ("Unable to switch to greeter: %s\n", error->message);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    else if (strcmp (command, "switch-to-user") == 0)
    {
        gchar *username, *session = "";

        if (n_options > 2)
        {
            g_printerr ("Usage switch-to-user USERNAME [SESSION]\n");
            usage ();
            return EXIT_FAILURE;
        }

        username = options[0];
        if (n_options == 2)
            session = options[1];

        if (!g_dbus_proxy_call_sync (get_seat_proxy (),
                                     "SwitchToUser",
                                     g_variant_new ("(ss)", username, session),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error))
        {
            g_printerr ("Unable to switch to user %s: %s\n", username, error->message);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    else if (strcmp (command, "switch-to-guest") == 0)
    {
        gchar *session = "";

        if (n_options > 1)
        {
            g_printerr ("Usage switch-to-guest [SESSION]\n");
            usage ();
            return EXIT_FAILURE;
        }

        if (n_options == 1)
            session = options[0];

        if (!g_dbus_proxy_call_sync (get_seat_proxy (),
                                     "SwitchToGuest",
                                     g_variant_new ("(s)", session),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error))
        {
            g_printerr ("Unable to switch to guest: %s\n", error->message);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    else if (strcmp (command, "lock") == 0)
    {
        if (n_options != 0)
        {
            g_printerr ("Usage lock\n");
            usage ();
            return EXIT_FAILURE;
        }

        if (!g_dbus_proxy_call_sync (get_seat_proxy (),
                                     "Lock",
                                     g_variant_new ("()"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error))
        {
            g_printerr ("Unable to lock seat: %s\n", error->message);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    else if (strcmp (command, "list-seats") == 0)
    {
        GVariant *seats, *sessions;
        GVariantIter *seat_iter;
        gchar *seat_path;

        if (!g_dbus_proxy_get_name_owner (dm_proxy))
        {
            g_printerr ("Unable to contact display manager\n");
            return EXIT_FAILURE;
        }
        seats = g_dbus_proxy_get_cached_property (dm_proxy, "Seats");

        g_variant_get (seats, "ao", &seat_iter);
        while (g_variant_iter_loop (seat_iter, "&o", &seat_path))
        {
            gchar *seat_name;
            GDBusProxy *proxy;
            gchar **property_names;
            GVariant *sessions;
            GVariantIter *session_iter;
            gchar *session_path;
            gint i;

            if (g_str_has_prefix (seat_path, "/org/freedesktop/DisplayManager/"))
                seat_name = seat_path + strlen ("/org/freedesktop/DisplayManager/");
            else
                seat_name = seat_path;

            proxy = g_dbus_proxy_new_sync (g_dbus_proxy_get_connection (dm_proxy),
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.DisplayManager",
                                           seat_path,
                                           "org.freedesktop.DisplayManager.Seat",
                                           NULL,
                                           NULL);
            if (!proxy || !g_dbus_proxy_get_name_owner (proxy))
                continue;

            g_print ("%s\n", seat_name);
            property_names = g_dbus_proxy_get_cached_property_names (proxy);
            for (i = 0; property_names[i]; i++)
            {
                GVariant *value;

                if (strcmp (property_names[i], "Sessions") == 0)
                    continue;

                value = g_dbus_proxy_get_cached_property (proxy, property_names[i]);
                g_print ("  %s=%s\n", property_names[i], g_variant_print (value, FALSE));
                g_variant_unref (value);
            }

            sessions = g_dbus_proxy_get_cached_property (proxy, "Sessions");
            if (!sessions)
                continue;

            g_variant_get (sessions, "ao", &session_iter);
            while (g_variant_iter_loop (session_iter, "&o", &session_path))
            {
                GDBusProxy *session_proxy;
                gchar *session_name;

                if (g_str_has_prefix (session_path, "/org/freedesktop/DisplayManager/"))
                    session_name = session_path + strlen ("/org/freedesktop/DisplayManager/");
                else
                    session_name = session_path;

                session_proxy = g_dbus_proxy_new_sync (g_dbus_proxy_get_connection (dm_proxy),
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       NULL,
                                                       "org.freedesktop.DisplayManager",
                                                       session_path,
                                                       "org.freedesktop.DisplayManager.Session",
                                                       NULL,
                                                       NULL);
                if (!session_proxy || !g_dbus_proxy_get_name_owner (session_proxy))
                    continue;

                g_print ("  %s\n", session_name);
                property_names = g_dbus_proxy_get_cached_property_names (session_proxy);
                for (i = 0; property_names[i]; i++)
                {
                    GVariant *value;

                    if (strcmp (property_names[i], "Seat") == 0)
                        continue;

                    value = g_dbus_proxy_get_cached_property (session_proxy, property_names[i]);
                    g_print ("    %s=%s\n", property_names[i], g_variant_print (value, FALSE));
                    g_variant_unref (value);
                }

                g_object_unref (session_proxy);
            }
            g_variant_iter_free (session_iter);

            g_object_unref (proxy);
        }
        g_variant_iter_free (seat_iter);

        return EXIT_SUCCESS;
    }
    else if (strcmp (command, "add-nested-seat") == 0)
    {
        gchar *path, *xephyr_command, **xephyr_argv;
        gchar *dimensions = NULL;
        GMainLoop *loop;

        path = g_find_program_in_path ("Xephyr");
        if (!path)
        {
            g_printerr ("Unable to find Xephyr, please install it\n");
            return EXIT_FAILURE;
        }

        if (n_options > 0)
        {
            /* Parse the given options */
            if (strcmp (options[0], "--fullscreen") == 0 && n_options == 1)
            {
                dimensions = "fullscreen";
            }
            else if (strcmp (options[0], "--screen") == 0 && n_options == 2)
            {
                dimensions = options[1];
            }
            else
            {
                g_printerr ("Usage add-nested-seat [--fullscreen|--screen DIMENSIONS]\n");
                usage ();
                return EXIT_FAILURE;
            }
        }

        /* Get a unique display number.  It's racy, but the only reliable method to get one */
        xephyr_display_number = 0;
        while (TRUE)
        {
            gchar *lock_name;
            gboolean has_lock;

            lock_name = g_strdup_printf ("/tmp/.X%d-lock", xephyr_display_number);
            has_lock = g_file_test (lock_name, G_FILE_TEST_EXISTS);
            g_free (lock_name);
          
            if (has_lock)
                xephyr_display_number++;
            else
                break;
        }

        /* Wait for signal from Xephyr is ready */
        signal (SIGUSR1, xephyr_signal_cb);

        if (dimensions == NULL)
        {
            xephyr_command = g_strdup_printf ("Xephyr :%d ", xephyr_display_number);
        }
        else if (strcmp (dimensions, "fullscreen") == 0)
        {
            xephyr_command = g_strdup_printf ("Xephyr :%d -fullscreen", xephyr_display_number);   
        }
        else
        {
            xephyr_command = g_strdup_printf ("Xephyr :%d -screen %s", xephyr_display_number, dimensions); 
        }
        if (!g_shell_parse_argv (xephyr_command, NULL, &xephyr_argv, &error) ||
            !g_spawn_async (NULL, xephyr_argv, NULL,
                            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                            xephyr_setup_cb, NULL,
                            &xephyr_pid, &error))
        {
            g_printerr ("Error running Xephyr: %s\n", error->message);
            exit (EXIT_FAILURE);
        }
        g_clear_error (&error);

        /* Block until ready */
        loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (loop);
    }
    else if (strcmp (command, "add-local-x-seat") == 0)
    {
        GVariant *result;
        gint display_number;
        const gchar *path;

        if (n_options != 1)
        {
            g_printerr ("Usage add-seat DISPLAY_NUMBER\n");
            usage ();
            return EXIT_FAILURE;
        }

        display_number = atoi (options[0]);

        result = g_dbus_proxy_call_sync (dm_proxy,
                                         "AddLocalXSeat",
                                         g_variant_new ("(i)", display_number),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);
        if (!result)
        {
            g_printerr ("Unable to add local X seat: %s\n", error->message);
            return EXIT_FAILURE;
        }

        if (!g_variant_is_of_type (result, G_VARIANT_TYPE ("(o)")))
        {
            g_printerr ("Unexpected response to AddLocalXSeat: %s\n", g_variant_get_type_string (result));
            return EXIT_FAILURE;
        }

        g_variant_get (result, "(&o)", &path);
        g_print ("%s\n", path);

        return EXIT_SUCCESS; 
    }
    else if (strcmp (command, "add-seat") == 0)
    {
        GVariant *result;
        gchar *type, *path;
        GVariantBuilder *properties;
        gint i;

        if (n_options < 1)
        {
            g_printerr ("Usage add-seat TYPE [NAME=VALUE...]\n");
            usage ();
            return EXIT_FAILURE;
        }

        type = options[0];
        properties = g_variant_builder_new (G_VARIANT_TYPE ("a(ss)"));
      
        for (i = 1; i < n_options; i++)
        {
            gchar *property, *name, *value;

            property = g_strdup (options[i]);
            name = property;
            value = strchr (property, '=');
            if (value)
            {
                *value = '\0';
                value++;
            }
            else
               value = "";

            g_variant_builder_add_value (properties, g_variant_new ("(ss)", name, value));
            g_free (property);
        }
      
        result = g_dbus_proxy_call_sync (dm_proxy,
                                         "AddSeat",
                                         g_variant_new ("(sa(ss))", type, properties),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);
        g_variant_builder_unref (properties);
        if (!result)
        {
            g_printerr ("Unable to add seat: %s\n", error->message);
            return EXIT_FAILURE;
        }

        if (!g_variant_is_of_type (result, G_VARIANT_TYPE ("(o)")))
        {
            g_printerr ("Unexpected response to AddSeat: %s\n", g_variant_get_type_string (result));
            return EXIT_FAILURE;
        }

        g_variant_get (result, "(&o)", &path);
        g_print ("%s\n", path);

        return EXIT_SUCCESS;
    }

    g_printerr ("Unknown command %s\n", command);
    usage ();
    return EXIT_FAILURE;
}
