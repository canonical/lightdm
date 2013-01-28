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

#include "configuration.h"
#include "display-manager.h"
#include "xdmcp-server.h"
#include "vnc-server.h"
#include "seat-xdmcp-session.h"
#include "seat-xvnc.h"
#include "xserver.h"
#include "process.h"
#include "session-child.h"

static gchar *config_path = NULL;
static GMainLoop *loop = NULL;
static GTimer *log_timer;
static int log_fd = -1;
static gboolean debug = FALSE;

static DisplayManager *display_manager = NULL;
static XDMCPServer *xdmcp_server = NULL;
static VNCServer *vnc_server = NULL;
static guint bus_id = 0;
static GDBusConnection *bus = NULL;
static guint bus_id;
static GDBusNodeInfo *seat_info;
static GHashTable *seat_bus_entries = NULL;
static guint seat_index = 0;
static GDBusNodeInfo *session_info;
static GHashTable *session_bus_entries = NULL;
static guint session_index = 0;
static gint exit_code = EXIT_SUCCESS;

typedef struct
{
    gchar *path;
    gchar *parent_path;
    gchar *removed_signal;
    guint bus_id;
} BusEntry;

#define LIGHTDM_BUS_NAME "org.freedesktop.DisplayManager"

static void
log_cb (const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer data)
{
    const gchar *prefix;
    gchar *text;

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

    text = g_strdup_printf ("[%+.2fs] %s %s\n", g_timer_elapsed (log_timer, NULL), prefix, message);

    /* Log everything to a file */
    if (log_fd >= 0)
    {
        ssize_t n_written;
        n_written = write (log_fd, text, strlen (text));
        if (n_written < 0)
            ; /* Check result so compiler doesn't warn about it */
    }

    /* Log to stderr if requested */
    if (debug)
        g_printerr ("%s", text);
    else
        g_log_default_handler (log_domain, log_level, message, data);

    g_free (text);
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

    log_fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    fcntl (log_fd, F_SETFD, FD_CLOEXEC);
    g_log_set_default_handler (log_cb, NULL);

    g_debug ("Logging to %s", path);
    g_free (path);
}

static void
signal_cb (Process *process, int signum)
{
    g_debug ("Caught %s signal, shutting down", g_strsignal (signum));
    display_manager_stop (display_manager);
    // FIXME: Stop XDMCP server
}

static gboolean
exit_cb (gpointer data)
{
    /* Clean up display manager */
    g_object_unref (display_manager);
    display_manager = NULL;

    /* Remove D-Bus interface */
    g_bus_unown_name (bus_id);
    if (seat_bus_entries)
        g_hash_table_unref (seat_bus_entries);
    if (session_bus_entries)
        g_hash_table_unref (session_bus_entries);

    g_debug ("Exiting with return value %d", exit_code);
    exit (exit_code);
}

static void
display_manager_stopped_cb (DisplayManager *display_manager)
{
    g_debug ("Stopping daemon");
    g_idle_add (exit_cb, NULL);
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
set_seat_properties (Seat *seat, const gchar *config_section)
{
    gchar **keys;
    gint i;

    keys = config_get_keys (config_get_instance (), "SeatDefaults");
    for (i = 0; keys[i]; i++)
    {
        gchar *value = config_get_string (config_get_instance (), "SeatDefaults", keys[i]);
        seat_set_property (seat, keys[i], value);
        g_free (value);
    }
    g_strfreev (keys);

    if (config_section)
    {
        keys = config_get_keys (config_get_instance (), config_section);
        for (i = 0; keys[i]; i++)
        {
            gchar *value = config_get_string (config_get_instance (), config_section, keys[i]);
            seat_set_property (seat, keys[i], value);
            g_free (value);
        }
        g_strfreev (keys);
    }
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
    if (g_strcmp0 (method_name, "AddSeat") == 0)
    {
        gchar *type;
        GVariantIter *property_iter;
        gchar *name, *value;
        Seat *seat;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(sa(ss))")))
            return;

        g_variant_get (parameters, "(&sa(ss))", &type, &property_iter);

        g_debug ("Adding seat of type %s", type);

        seat = seat_new (type);
        if (seat)
        {
            set_seat_properties (seat, NULL);
            while (g_variant_iter_loop (property_iter, "(&s&s)", &name, &value))
                seat_set_property (seat, name, value);
        }
        g_variant_iter_free (property_iter);

        if (!seat)
        {
            // FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Unable to create seat of type %s", type);
            return;
        }

        if (display_manager_add_seat (display_manager, seat))
        {
            BusEntry *entry;

            entry = g_hash_table_lookup (seat_bus_entries, seat);
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", entry->path));
        }
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to start seat");
        g_object_unref (seat);
    }
    else if (g_strcmp0 (method_name, "AddLocalXSeat") == 0)
    {
        gint display_number;
        Seat *seat;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(i)")))
            return;

        g_variant_get (parameters, "(i)", &display_number);

        g_debug ("Adding local X seat :%d", display_number);

        seat = seat_new ("xremote");
        if (seat)
        {
            gchar *display_number_string;

            set_seat_properties (seat, NULL);
            display_number_string = g_strdup_printf ("%d", display_number);
            seat_set_property (seat, "xserver-display-number", display_number_string);
            g_free (display_number_string);
        }

        if (!seat)
        {
            // FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Unable to create local X seat");
            return;
        }

        if (display_manager_add_seat (display_manager, seat))
        {
            BusEntry *entry;

            entry = g_hash_table_lookup (seat_bus_entries, seat);
            g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", entry->path));
        }
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to start seat");
        g_object_unref (seat);
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
    if (g_strcmp0 (property_name, "HasGuestAccount") == 0)
        result = g_variant_new_boolean (seat_get_allow_guest (seat));
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
        const gchar *username, *session_name;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(ss)")))
            return;

        g_variant_get (parameters, "(&s&s)", &username, &session_name);
        if (strcmp (session_name, "") == 0)
            session_name = NULL;

        seat_switch_to_user (seat, username, session_name);
        g_dbus_method_invocation_return_value (invocation, NULL);
    }
    else if (g_strcmp0 (method_name, "SwitchToGuest") == 0)
    {
        const gchar *session_name;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
            return;

        g_variant_get (parameters, "(&s)", &session_name);
        if (strcmp (session_name, "") == 0)
            session_name = NULL;

        seat_switch_to_guest (seat, session_name);
        g_dbus_method_invocation_return_value (invocation, NULL);
    }
    else if (g_strcmp0 (method_name, "Lock") == 0)
    {
        /* FIXME: Should only allow locks if have a session on this seat */
        seat_lock (seat, NULL);
        g_dbus_method_invocation_return_value (invocation, NULL);
    }
}

static Seat *
get_seat_for_session (Session *session)
{
    GList *seat_link;

    for (seat_link = display_manager_get_seats (display_manager); seat_link; seat_link = seat_link->next)
    {
        Seat *seat = seat_link->data;
        GList *display_link;

        for (display_link = seat_get_displays (seat); display_link; display_link = display_link->next)
        {
            Display *display = display_link->data;

            if (display_get_session (display) == session)
                return seat;
        }
    } 

    return NULL;
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
        return g_variant_new_string (session_get_username (session));

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
    Session *session = user_data;

    if (g_strcmp0 (method_name, "Lock") == 0)
    {
        Seat *seat;

        seat = get_seat_for_session (session);
        /* FIXME: Should only allow locks if have a session on this seat */
        seat_lock (seat, session_get_username (session));
        g_dbus_method_invocation_return_value (invocation, NULL);
    }
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

static gboolean
start_session_cb (Display *display, Seat *seat)
{
    Session *session;
    BusEntry *seat_entry;
    gchar *path;

    session = display_get_session (display);

    /* Set environment variables when session runs */
    seat_entry = g_hash_table_lookup (seat_bus_entries, seat);
    session_set_env (session, "XDG_SEAT_PATH", seat_entry->path);
    path = g_strdup_printf ("/org/freedesktop/DisplayManager/Session%d", session_index);
    session_index++;
    session_set_env (session, "XDG_SESSION_PATH", path);
    g_object_set_data_full (G_OBJECT (session), "XDG_SESSION_PATH", path, g_free);

    return FALSE;
}

static void
session_stopped_cb (Session *session, Seat *seat)
{
    g_signal_handlers_disconnect_matched (session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    g_hash_table_remove (session_bus_entries, session);
}

static gboolean
session_started_cb (Display *display, Seat *seat)
{
    static const GDBusInterfaceVTable session_vtable =
    {
        handle_session_call,
        handle_session_get_property
    };
    Session *session;
    BusEntry *seat_entry, *entry;

    session = display_get_session (display);

    g_signal_connect (session, "stopped", G_CALLBACK (session_stopped_cb), seat);

    seat_entry = g_hash_table_lookup (seat_bus_entries, seat);
    entry = bus_entry_new (g_object_get_data (G_OBJECT (session), "XDG_SESSION_PATH"), seat_entry ? seat_entry->path : NULL, "SessionRemoved");
    g_hash_table_insert (session_bus_entries, g_object_ref (session), entry);

    g_debug ("Registering session with bus path %s", entry->path);

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

    return FALSE;
}

static void
display_added_cb (Seat *seat, Display *display)
{
    g_signal_connect (display, "start-session", G_CALLBACK (start_session_cb), seat);
    g_signal_connect_after (display, "start-session", G_CALLBACK (session_started_cb), seat);
}

static void
seat_added_cb (DisplayManager *display_manager, Seat *seat)
{
    static const GDBusInterfaceVTable seat_vtable =
    {
        handle_seat_call,
        handle_seat_get_property
    };
    GList *link;
    gchar *path;
    BusEntry *entry;

    g_signal_connect (seat, "display-added", G_CALLBACK (display_added_cb), NULL);
    for (link = seat_get_displays (seat); link; link = link->next)
        display_added_cb (seat, (Display *) link->data);

    path = g_strdup_printf ("/org/freedesktop/DisplayManager/Seat%d", seat_index);
    seat_index++;

    entry = bus_entry_new (path, NULL, "SeatRemoved");
    g_free (path);
    g_hash_table_insert (seat_bus_entries, g_object_ref (seat), entry);

    g_debug ("Registering seat with bus path %s", entry->path);

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
seat_removed_cb (DisplayManager *display_manager, Seat *seat)
{
    gboolean exit_on_failure;

    exit_on_failure = seat_get_boolean_property (seat, "exit-on-failure");
    g_hash_table_remove (seat_bus_entries, seat);

    if (exit_on_failure)
    {
        g_debug ("Required seat has stopped");
        exit_code = EXIT_FAILURE;
        display_manager_stop (display_manager);
    }
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
        "    <method name='AddSeat'>"
        "      <arg name='type' direction='in' type='s'/>"
        "      <arg name='properties' direction='in' type='a(ss)'/>"
        "      <arg name='seat' direction='out' type='o'/>"
        "    </method>"
        "    <method name='AddLocalXSeat'>"
        "      <arg name='display-number' direction='in' type='i'/>"
        "      <arg name='seat' direction='out' type='o'/>"
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
        "    <property name='HasGuestAccount' type='b' access='read'/>"
        "    <property name='Sessions' type='ao' access='read'/>"
        "    <method name='SwitchToGreeter'/>"
        "    <method name='SwitchToUser'>"
        "      <arg name='username' direction='in' type='s'/>"
        "      <arg name='session-name' direction='in' type='s'/>"
        "    </method>"
        "    <method name='SwitchToGuest'>"
        "      <arg name='session-name' direction='in' type='s'/>"
        "    </method>"
        "    <method name='Lock'/>"    
        "  </interface>"
        "</node>";
    const gchar *session_interface =
        "<node>"
        "  <interface name='org.freedesktop.DisplayManager.Session'>"
        "    <property name='Seat' type='o' access='read'/>"
        "    <property name='UserName' type='s' access='read'/>"
        "    <method name='Lock'/>"
        "  </interface>"
        "</node>";
    GDBusNodeInfo *display_manager_info;
    GList *link;

    g_debug ("Acquired bus name %s", name);

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
    g_dbus_node_info_unref  (display_manager_info);

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
        g_printerr ("Failed to use bus name " LIGHTDM_BUS_NAME ", do you have appropriate permissions?\n");
    else
        g_printerr ("Failed to get D-Bus connection\n");

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

static gboolean
xdmcp_session_cb (XDMCPServer *server, XDMCPSession *session)
{
    SeatXDMCPSession *seat;
    gboolean result;

    seat = seat_xdmcp_session_new (session);
    set_seat_properties (SEAT (seat), NULL);
    result = display_manager_add_seat (display_manager, SEAT (seat));
    g_object_unref (seat);

    return result;
}

static void
vnc_connection_cb (VNCServer *server, GSocket *connection)
{
    SeatXVNC *seat;

    seat = seat_xvnc_new (connection);
    set_seat_properties (SEAT (seat), NULL);
    display_manager_add_seat (display_manager, SEAT (seat));
    g_object_unref (seat);
}

int
main (int argc, char **argv)
{
    FILE *pid_file;
    GOptionContext *option_context;
    gboolean result;
    gchar **groups, **i, *dir;
    gint n_seats = 0;
    gboolean explicit_config = FALSE;
    gboolean test_mode = FALSE;
    gchar *pid_path = "/var/run/lightdm.pid";
    gchar *xsessions_dir = NULL;
    gchar *remote_sessions_dir = NULL;
    gchar *xgreeters_dir = NULL;
    gchar *config_dir;
    gchar *log_dir = NULL;
    gchar *run_dir = NULL;
    gchar *cache_dir = NULL;
    gchar *default_log_dir = g_strdup (LOG_DIR);
    gchar *default_run_dir = g_strdup (RUN_DIR);
    gchar *default_cache_dir = g_strdup (CACHE_DIR);
    gboolean show_version = FALSE;
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
        { "xsessions-dir", 0, 0, G_OPTION_ARG_STRING, &xsessions_dir,
          /* Help string for command line --xsessions-dir flag */
          N_("Directory to load X sessions from"), "DIRECTORY" },
        { "remote-sessions-dir", 0, 0, G_OPTION_ARG_STRING, &remote_sessions_dir,
          /* Help string for command line --remote-sessions-dir flag */
          N_("Directory to load remote sessions from"), "DIRECTORY" },
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
          N_("Directory to cache information"), "DIRECTORY" },
        { "version", 'v', 0, G_OPTION_ARG_NONE, &show_version,
          /* Help string for command line --version flag */
          N_("Show release version"), NULL },
        { NULL }
    };
    GError *error = NULL;

    /* When lightdm starts sessions it needs to run itself in a new mode */
    if (argc >= 2 && strcmp (argv[1], "--session-child") == 0)
        return session_child_run (argc, argv);

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif
    loop = g_main_loop_new (NULL, FALSE);

    g_signal_connect (process_get_current (), "got-signal", G_CALLBACK (signal_cb), NULL);

    option_context = g_option_context_new (/* Arguments and description for --help test */
                                           _("- Display Manager"));
    g_option_context_add_main_entries (option_context, options, GETTEXT_PACKAGE);
    result = g_option_context_parse (option_context, &argc, &argv, &error);
    if (error)
        g_printerr ("%s\n", error->message);
    g_clear_error (&error);
    g_option_context_free (option_context);
    if (!result)
    {
        g_printerr (/* Text printed out when an unknown command-line argument provided */
                    _("Run '%s --help' to see a full list of available command line options."), argv[0]);
        g_printerr ("\n");
        return EXIT_FAILURE;
    }

    if (show_version)
    {
        /* NOTE: Is not translated so can be easily parsed */
        g_printerr ("lightdm %s\n", VERSION);
        return EXIT_SUCCESS;
    }

    if (config_path)
    {
        config_dir = g_path_get_basename (config_path);
        config_dir = path_make_absolute (config_dir);
        explicit_config = TRUE;
    }
    else
    {
        config_dir = g_strdup (CONFIG_DIR);
        config_path = g_build_filename (config_dir, "lightdm.conf", NULL);
    }
    config_set_string (config_get_instance (), "LightDM", "config-directory", config_dir);
    g_free (config_dir);

    if (!test_mode && getuid () != 0)
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

    /* Make sure the system binary directory (where the greeters are installed) is in the path */
    if (test_mode)
    {
        const gchar *path = g_getenv ("PATH");
        gchar *new_path;

        if (path)
            new_path = g_strdup_printf ("%s:%s", path, SBIN_DIR);
        else
            new_path = g_strdup (SBIN_DIR);
        g_setenv ("PATH", new_path, TRUE);
        g_free (new_path);
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
    remote_sessions_dir = path_make_absolute (remote_sessions_dir);
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
        gboolean is_empty;

        is_empty = error && g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
      
        if (explicit_config || !is_empty)      
        {
            if (error)
                g_printerr ("Failed to load configuration from %s: %s\n", config_path, error->message);
            exit (EXIT_FAILURE);
        }
    }
    g_clear_error (&error);

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
    if (!config_has_key (config_get_instance (), "SeatDefaults", "type"))
        config_set_string (config_get_instance (), "SeatDefaults", "type", "xlocal");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "xserver-command"))
        config_set_string (config_get_instance (), "SeatDefaults", "xserver-command", "X");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "start-session"))
        config_set_boolean (config_get_instance (), "SeatDefaults", "start-session", TRUE);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "allow-guest"))
        config_set_boolean (config_get_instance (), "SeatDefaults", "allow-guest", TRUE);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "greeter-allow-guest"))
        config_set_boolean (config_get_instance (), "SeatDefaults", "greeter-allow-guest", TRUE);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "greeter-show-remote-login"))
        config_set_boolean (config_get_instance (), "SeatDefaults", "greeter-show-remote-login", TRUE);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "greeter-session"))
        config_set_string (config_get_instance (), "SeatDefaults", "greeter-session", GREETER_SESSION);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "user-session"))
        config_set_string (config_get_instance (), "SeatDefaults", "user-session", USER_SESSION);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "session-wrapper"))
        config_set_string (config_get_instance (), "SeatDefaults", "session-wrapper", "lightdm-session");
    if (!config_has_key (config_get_instance (), "LightDM", "log-directory"))
        config_set_string (config_get_instance (), "LightDM", "log-directory", default_log_dir);
    g_free (default_log_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "run-directory"))
        config_set_string (config_get_instance (), "LightDM", "run-directory", default_run_dir);
    g_free (default_run_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "cache-directory"))
        config_set_string (config_get_instance (), "LightDM", "cache-directory", default_cache_dir);
    g_free (default_cache_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "xsessions-directory"))
        config_set_string (config_get_instance (), "LightDM", "xsessions-directory", XSESSIONS_DIR);
    if (!config_has_key (config_get_instance (), "LightDM", "remote-sessions-directory"))
        config_set_string (config_get_instance (), "LightDM", "remote-sessions-directory", REMOTE_SESSIONS_DIR);
    if (!config_has_key (config_get_instance (), "LightDM", "xgreeters-directory"))
        config_set_string (config_get_instance (), "LightDM", "xgreeters-directory", XGREETERS_DIR);

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
    if (xsessions_dir)
        config_set_string (config_get_instance (), "LightDM", "xsessions-directory", xsessions_dir);
    g_free (xsessions_dir);
    if (remote_sessions_dir)
        config_set_string (config_get_instance (), "LightDM", "remote-sessions-directory", remote_sessions_dir);
    g_free (remote_sessions_dir);
    if (xgreeters_dir)
        config_set_string (config_get_instance (), "LightDM", "xgreeters-directory", xgreeters_dir);
    g_free (xgreeters_dir);

    /* Create run and cache directories */
    dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    g_mkdir_with_parents (dir, S_IRWXU | S_IXGRP | S_IXOTH);
    g_free (dir);
    dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
    g_mkdir_with_parents (dir, S_IRWXU | S_IXGRP | S_IXOTH);
    g_free (dir);
    dir = config_get_string (config_get_instance (), "LightDM", "cache-directory");
    g_mkdir_with_parents (dir, S_IRWXU | S_IXGRP | S_IXOTH);
    g_free (dir);

    log_init ();

    g_debug ("Starting Light Display Manager %s, UID=%i PID=%i", VERSION, getuid (), getpid ());

    g_debug ("Loaded configuration from %s", config_path);
    g_free (config_path);

    g_debug ("Using D-Bus name %s", LIGHTDM_BUS_NAME);
    bus_id = g_bus_own_name (getuid () == 0 ? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION,
                             LIGHTDM_BUS_NAME,
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             bus_acquired_cb,
                             NULL,
                             name_lost_cb,
                             NULL,
                             NULL);

    if (getuid () != 0)
        g_debug ("Running in user mode");
    if (getenv ("DISPLAY"))
        g_debug ("Using Xephyr for X servers");

    display_manager = display_manager_new ();
    g_signal_connect (display_manager, "stopped", G_CALLBACK (display_manager_stopped_cb), NULL);

    /* Load the static display entries */
    groups = config_get_groups (config_get_instance ());
    for (i = groups; *i; i++)
    {
        gchar *config_section = *i;
        gchar *type;
        Seat *seat;

        if (!g_str_has_prefix (config_section, "Seat:"))
            continue;

        g_debug ("Loading seat %s", config_section);
        type = config_get_string (config_get_instance (), config_section, "type");
        if (!type)
            type = config_get_string (config_get_instance (), "SeatDefaults", "type");
        seat = seat_new (type);
        g_free (type);
        if (seat)
        {
            set_seat_properties (seat, config_section);
            display_manager_add_seat (display_manager, seat);
            g_object_unref (seat);
            n_seats++;
        }
        else
            g_warning ("Failed to create seat %s", config_section);
    }
    g_strfreev (groups);

    /* If no seats start a default one */
    if (n_seats == 0 && config_get_boolean (config_get_instance (), "LightDM", "start-default-seat"))
    {
        gchar *type;
        Seat *seat;

        g_debug ("Adding default seat");

        type = config_get_string (config_get_instance (), "SeatDefaults", "type");
        seat = seat_new (type);
        g_free (type);
        if (seat)
        {
            set_seat_properties (seat, NULL);
            seat_set_property (seat, "exit-on-failure", "true");
            display_manager_add_seat (display_manager, seat);
            g_object_unref (seat);
        }
        else
            g_warning ("Failed to create default seat");
    }

    display_manager_start (display_manager);

    /* Start the XDMCP server */
    if (config_get_boolean (config_get_instance (), "XDMCPServer", "enabled"))
    {
        gchar *key_name, *key = NULL;

        xdmcp_server = xdmcp_server_new ();
        if (config_has_key (config_get_instance (), "XDMCPServer", "port"))
        {
            gint port;
            port = config_get_integer (config_get_instance (), "XDMCPServer", "port");
            if (port > 0)
                xdmcp_server_set_port (xdmcp_server, port);
        }
        g_signal_connect (xdmcp_server, "new-session", G_CALLBACK (xdmcp_session_cb), NULL);

        key_name = config_get_string (config_get_instance (), "XDMCPServer", "key");
        if (key_name)
        {
            gchar *dir, *path;
            GKeyFile *keys;
            gboolean result;
            GError *error = NULL;

            dir = config_get_string (config_get_instance (), "LightDM", "config-directory");
            path = g_build_filename (dir, "keys.conf", NULL);
            g_free (dir);

            keys = g_key_file_new ();
            result = g_key_file_load_from_file (keys, path, G_KEY_FILE_NONE, &error);
            if (error)
                g_debug ("Error getting key %s", error->message);
            g_clear_error (&error);

            if (result)
            {
                if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                    key = g_key_file_get_string (keys, "keyring", key_name, NULL);
                else
                    g_debug ("Key %s not defined", key_name);
            }
            g_free (path);
            g_key_file_free (keys);
        }
        if (key)
            xdmcp_server_set_key (xdmcp_server, key);
        g_free (key_name);
        g_free (key);

        g_debug ("Starting XDMCP server on UDP/IP port %d", xdmcp_server_get_port (xdmcp_server));
        xdmcp_server_start (xdmcp_server);
    }

    /* Start the VNC server */
    if (config_get_boolean (config_get_instance (), "VNCServer", "enabled"))
    {
        gchar *path;

        path = g_find_program_in_path ("Xvnc");
        if (path)
        {
            vnc_server = vnc_server_new ();
            if (config_has_key (config_get_instance (), "VNCServer", "port"))
            {
                gint port;
                port = config_get_integer (config_get_instance (), "VNCServer", "port");
                if (port > 0)
                    vnc_server_set_port (vnc_server, port);
            }
            g_signal_connect (vnc_server, "new-connection", G_CALLBACK (vnc_connection_cb), NULL);

            g_debug ("Starting VNC server on TCP/IP port %d", vnc_server_get_port (vnc_server));
            vnc_server_start (vnc_server);

            g_free (path);
        }
        else
            g_warning ("Can't start VNC server, Xvn is not in the path");
    }

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
