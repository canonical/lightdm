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
static XDMCPServer *xdmcp_server = NULL;
static VNCServer *vnc_server = NULL;
static guint bus_id = 0;
static GDBusConnection *bus = NULL;
static guint reg_id = 0;
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
    guint bus_id;
} BusEntry;

#define LIGHTDM_BUS_NAME "org.freedesktop.DisplayManager"

static gboolean update_login1_seat (Login1Seat *login1_seat);

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
    gboolean backup_logs;

    log_timer = g_timer_new ();

    /* Log to a file */
    log_dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    path = g_build_filename (log_dir, "lightdm.log", NULL);
    g_free (log_dir);

    backup_logs = config_get_boolean (config_get_instance (), "LightDM", "backup-logs");
    log_fd = log_file_open (path, backup_logs ? LOG_MODE_BACKUP_AND_TRUNCATE : LOG_MODE_APPEND);
    fcntl (log_fd, F_SETFD, FD_CLOEXEC);
    g_log_set_default_handler (log_cb, NULL);

    g_debug ("Logging to %s", path);
    g_free (path);
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

static void
display_manager_seat_removed_cb (DisplayManager *display_manager, Seat *seat)
{
    gchar **types;
    gchar **iter;
    Seat *next_seat = NULL;
    GString *next_types;

    /* If we have fallback types registered for the seat, let's try them
       before giving up. */
    types = seat_get_string_list_property (seat, "type");
    next_types = g_string_new ("");
    for (iter = types; iter && *iter; iter++)
    {
        if (iter == types)
            continue; // skip first one, that is our current seat type

        if (!next_seat)
        {
            next_seat = seat_new (*iter);
            g_string_assign (next_types, *iter);
        }
        else
        {
            // Build up list of types to try next time
            g_string_append_c (next_types, ';');
            g_string_append (next_types, *iter);
        }
    }
    g_strfreev (types);

    if (next_seat)
    {
        const gchar *seat_name;
        gchar *config_section = NULL;

        seat_name = seat_get_string_property (seat, "seat-name");
        if (seat_name)
            config_section = g_strdup_printf ("Seat:%s", seat_name);
        set_seat_properties (next_seat, config_section);
        g_free (config_section);

        // We set this manually on default seat.  Let's port it over if needed.
        if (seat_get_boolean_property (seat, "exit-on-failure"))
            seat_set_property (next_seat, "exit-on-failure", "true");

        seat_set_property (next_seat, "type", next_types->str);

        display_manager_add_seat (display_manager, next_seat);
        g_object_unref (next_seat);
    }
    else if (seat_get_boolean_property (seat, "exit-on-failure"))
    {
        g_debug ("Required seat has stopped");
        exit_code = EXIT_FAILURE;
        display_manager_stop (display_manager);
    }

    g_string_free (next_types, TRUE);
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
        for (link = seat_get_sessions (seat); link; link = link->next)
        {
            Session *session = link->data;
            BusEntry *entry;
            entry = g_hash_table_lookup (session_bus_entries, session);
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
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");

        if (seat_switch_to_greeter (seat))
            g_dbus_method_invocation_return_value (invocation, NULL);
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to switch to greeter");
    }
    else if (g_strcmp0 (method_name, "SwitchToUser") == 0)
    {
        const gchar *username, *session_name;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(ss)")))
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");

        g_variant_get (parameters, "(&s&s)", &username, &session_name);
        if (strcmp (session_name, "") == 0)
            session_name = NULL;

        if (seat_switch_to_user (seat, username, session_name))
            g_dbus_method_invocation_return_value (invocation, NULL);
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to switch to user");
    }
    else if (g_strcmp0 (method_name, "SwitchToGuest") == 0)
    {
        const gchar *session_name;

        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");

        g_variant_get (parameters, "(&s)", &session_name);
        if (strcmp (session_name, "") == 0)
            session_name = NULL;

        if (seat_switch_to_guest (seat, session_name))
            g_dbus_method_invocation_return_value (invocation, NULL);
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to switch to guest");
    }
    else if (g_strcmp0 (method_name, "Lock") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("()")))
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");

        /* FIXME: Should only allow locks if have a session on this seat */
        if (seat_lock (seat, NULL))
            g_dbus_method_invocation_return_value (invocation, NULL);
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to lock seat");
    }
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
}

static Seat *
get_seat_for_session (Session *session)
{
    GList *seat_link;

    for (seat_link = display_manager_get_seats (display_manager); seat_link; seat_link = seat_link->next)
    {
        Seat *seat = seat_link->data;
        GList *session_link;

        for (session_link = seat_get_sessions (seat); session_link; session_link = session_link->next)
        {
            Session *s = session_link->data;

            if (s == session)
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
bus_entry_new (const gchar *path, const gchar *parent_path)
{
    BusEntry *entry;

    entry = g_malloc0 (sizeof (BusEntry));
    entry->path = g_strdup (path);
    entry->parent_path = g_strdup (parent_path);

    return entry;
}

static void
bus_entry_free (gpointer data)
{
    BusEntry *entry = data;

    g_free (entry->path);
    g_free (entry->parent_path);
    g_free (entry);
}

static void
running_user_session_cb (Seat *seat, Session *session)
{
    static const GDBusInterfaceVTable session_vtable =
    {
        handle_session_call,
        handle_session_get_property
    };
    BusEntry *seat_entry, *entry;
    gchar *path;
    GError *error = NULL;

    /* Set environment variables when session runs */
    seat_entry = g_hash_table_lookup (seat_bus_entries, seat);
    session_set_env (session, "XDG_SEAT_PATH", seat_entry->path);
    path = g_strdup_printf ("/org/freedesktop/DisplayManager/Session%d", session_index);
    session_index++;
    session_set_env (session, "XDG_SESSION_PATH", path);
    g_object_set_data_full (G_OBJECT (session), "XDG_SESSION_PATH", path, g_free);

    seat_entry = g_hash_table_lookup (seat_bus_entries, seat);
    entry = bus_entry_new (g_object_get_data (G_OBJECT (session), "XDG_SESSION_PATH"), seat_entry ? seat_entry->path : NULL);
    g_hash_table_insert (session_bus_entries, g_object_ref (session), entry);

    g_debug ("Registering session with bus path %s", entry->path);

    entry->bus_id = g_dbus_connection_register_object (bus,
                                                       entry->path,
                                                       session_info->interfaces[0],
                                                       &session_vtable,
                                                       g_object_ref (session), g_object_unref,
                                                       &error);
    if (entry->bus_id == 0)
        g_warning ("Failed to register user session: %s", error->message);
    g_clear_error (&error);

    if (!g_dbus_connection_emit_signal (bus,
                                        NULL,
                                        "/org/freedesktop/DisplayManager",
                                        "org.freedesktop.DisplayManager",
                                        "SessionAdded",
                                        g_variant_new ("(o)", entry->path),
                                        &error))
        g_warning ("Failed to emit SessionAdded signal: %s", error->message);
    g_clear_error (&error);
}

static void
session_removed_cb (Seat *seat, Session *session)
{
    BusEntry *entry;

    g_signal_handlers_disconnect_matched (session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);

    entry = g_hash_table_lookup (session_bus_entries, session);
    if (entry)
    {
        GError *error = NULL;

        g_dbus_connection_unregister_object (bus, entry->bus_id);
        if (!g_dbus_connection_emit_signal (bus,
                                            NULL,
                                            "/org/freedesktop/DisplayManager",
                                            "org.freedesktop.DisplayManager",
                                            "SessionRemoved",
                                            g_variant_new ("(o)", entry->path),
                                            &error))
            g_warning ("Failed to emit SessionRemoved signal: %s", error->message);
        g_clear_error (&error);
    }

    g_hash_table_remove (session_bus_entries, session);
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
    GError *error = NULL;

    path = g_strdup_printf ("/org/freedesktop/DisplayManager/Seat%d", seat_index);
    seat_index++;

    entry = bus_entry_new (path, NULL);
    g_free (path);
    g_hash_table_insert (seat_bus_entries, g_object_ref (seat), entry);

    g_debug ("Registering seat with bus path %s", entry->path);

    entry->bus_id = g_dbus_connection_register_object (bus,
                                                       entry->path,
                                                       seat_info->interfaces[0],
                                                       &seat_vtable,
                                                       g_object_ref (seat), g_object_unref,
                                                       &error);
    if (entry->bus_id == 0)
        g_warning ("Failed to register seat: %s", error->message);
    g_clear_error (&error);

    if (!g_dbus_connection_emit_signal (bus,
                                        NULL,
                                        "/org/freedesktop/DisplayManager",
                                        "org.freedesktop.DisplayManager",
                                        "SeatAdded",
                                        g_variant_new ("(o)", entry->path),
                                        &error))
        g_warning ("Failed to emit SeatAdded signal: %s", error->message);
    g_clear_error (&error);

    g_signal_connect (seat, "running-user-session", G_CALLBACK (running_user_session_cb), NULL);
    g_signal_connect (seat, "session-removed", G_CALLBACK (session_removed_cb), NULL);
}

static void
seat_removed_cb (DisplayManager *display_manager, Seat *seat)
{
    BusEntry *entry;

    entry = g_hash_table_lookup (seat_bus_entries, seat);
    if (entry)
    {
        GError *error = NULL;

        g_dbus_connection_unregister_object (bus, entry->bus_id);
        if (!g_dbus_connection_emit_signal (bus,
                                            NULL,
                                            "/org/freedesktop/DisplayManager",
                                            "org.freedesktop.DisplayManager",
                                            "SeatRemoved",
                                            g_variant_new ("(o)", entry->path),
                                            &error))
            g_warning ("Failed to emit SeatRemoved signal: %s", error->message);
        g_clear_error (&error);
    }

    g_hash_table_remove (seat_bus_entries, seat);
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
    GError *error = NULL;

    g_debug ("Acquired bus name %s", name);

    bus = connection;

    display_manager_info = g_dbus_node_info_new_for_xml (display_manager_interface, NULL);
    g_assert (display_manager_info != NULL);
    seat_info = g_dbus_node_info_new_for_xml (seat_interface, NULL);
    g_assert (seat_info != NULL);
    session_info = g_dbus_node_info_new_for_xml (session_interface, NULL);
    g_assert (session_info != NULL);

    reg_id = g_dbus_connection_register_object (connection,
                                                "/org/freedesktop/DisplayManager",
                                                display_manager_info->interfaces[0],
                                                &display_manager_vtable,
                                                NULL, NULL,
                                                &error);
    if (reg_id == 0)
        g_warning ("Failed to register display manager: %s", error->message);
    g_clear_error (&error);
    g_dbus_node_info_unref (display_manager_info);

    seat_bus_entries = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, bus_entry_free);
    session_bus_entries = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, bus_entry_free);

    g_signal_connect (display_manager, "seat-added", G_CALLBACK (seat_added_cb), NULL);
    g_signal_connect (display_manager, "seat-removed", G_CALLBACK (seat_removed_cb), NULL);
    for (link = display_manager_get_seats (display_manager); link; link = link->next)
        seat_added_cb (display_manager, (Seat *) link->data);

    display_manager_start (display_manager);

    /* Start the XDMCP server */
    if (config_get_boolean (config_get_instance (), "XDMCPServer", "enabled"))
    {
        gchar *key_name, *key = NULL, *listen_address, *hostname;

        xdmcp_server = xdmcp_server_new ();
        if (config_has_key (config_get_instance (), "XDMCPServer", "port"))
        {
            gint port;
            port = config_get_integer (config_get_instance (), "XDMCPServer", "port");
            if (port > 0)
                xdmcp_server_set_port (xdmcp_server, port);
        }
        listen_address = config_get_string (config_get_instance (), "XDMCPServer", "listen-address");
        xdmcp_server_set_listen_address (xdmcp_server, listen_address);
        g_free (listen_address);
        hostname = config_get_string (config_get_instance (), "XDMCPServer", "hostname");
        xdmcp_server_set_hostname (xdmcp_server, hostname);
        g_free (hostname);
        g_signal_connect (xdmcp_server, "new-session", G_CALLBACK (xdmcp_session_cb), NULL);

        key_name = config_get_string (config_get_instance (), "XDMCPServer", "key");
        if (key_name)
        {
            gchar *path;
            GKeyFile *keys;
            gboolean result;
            GError *error = NULL;

            path = g_build_filename (config_get_directory (config_get_instance ()), "keys.conf", NULL);

            keys = g_key_file_new ();
            result = g_key_file_load_from_file (keys, path, G_KEY_FILE_NONE, &error);
            if (error)
                g_warning ("Unable to load keys from %s: %s", path, error->message);
            g_clear_error (&error);

            if (result)
            {
                if (g_key_file_has_key (keys, "keyring", key_name, NULL))
                    key = g_key_file_get_string (keys, "keyring", key_name, NULL);
                else
                    g_warning ("Key %s not defined", key_name);
            }
            g_free (path);
            g_key_file_free (keys);
        }
        if (key)
            xdmcp_server_set_key (xdmcp_server, key);
        g_free (key_name);
        g_free (key);
      
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
        gchar *path;

        path = g_find_program_in_path ("Xvnc");
        if (path)
        {
            gchar *listen_address;

            vnc_server = vnc_server_new ();
            if (config_has_key (config_get_instance (), "VNCServer", "port"))
            {
                gint port;
                port = config_get_integer (config_get_instance (), "VNCServer", "port");
                if (port > 0)
                    vnc_server_set_port (vnc_server, port);
            }
            listen_address = config_get_string (config_get_instance (), "VNCServer", "listen-address");
            vnc_server_set_listen_address (vnc_server, listen_address);
            g_free (listen_address);
            g_signal_connect (vnc_server, "new-connection", G_CALLBACK (vnc_connection_cb), NULL);

            g_debug ("Starting VNC server on TCP/IP port %d", vnc_server_get_port (vnc_server));
            vnc_server_start (vnc_server);

            g_free (path);
        }
        else
            g_warning ("Can't start VNC server, Xvnc is not in the path");
    }
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

static gboolean
add_login1_seat (Login1Seat *login1_seat)
{
    const gchar *seat_name = login1_seat_get_id (login1_seat);
    gchar **groups, **i;
    gchar *config_section = NULL;
    gchar **types = NULL, **type;
    Seat *seat = NULL;
    gboolean is_seat0, started = FALSE;

    g_debug ("New seat added from logind: %s", seat_name);
    is_seat0 = strcmp (seat_name, "seat0") == 0;

    groups = config_get_groups (config_get_instance ());
    for (i = groups; !config_section && *i; i++)
    {
        if (g_str_has_prefix (*i, "Seat:") &&
            g_str_has_suffix (*i, seat_name))
        {
            config_section = g_strdup (*i);
            break;
        }
    }
    g_strfreev (groups);

    if (config_section)
    {
        g_debug ("Loading properties from config section %s", config_section);
        types = config_get_string_list (config_get_instance (), config_section, "type");
    }

    if (!types)
        types = config_get_string_list (config_get_instance (), "SeatDefaults", "type");
    for (type = types; !seat && type && *type; type++)
        seat = seat_new (*type);
    g_strfreev (types);

    if (seat)
    {
        seat_set_property (seat, "xdg-seat", seat_name);
        set_seat_properties (seat, NULL);

        if (!login1_seat_get_can_multi_session (login1_seat))
        {
            g_debug ("Seat %s has property CanMultiSession=no", seat_name);
            /* XXX: uncomment this line after bug #1371250 is closed.
            seat_set_property (seat, "allow-user-switching", "false"); */
        }

        if (config_section)
            set_seat_properties (seat, config_section);

        if (is_seat0)
            seat_set_property (seat, "exit-on-failure", "true");
    }
    else
        g_debug ("Unable to create seat: %s", seat_name);

    if (seat)
    {
        started = display_manager_add_seat (display_manager, seat);
        if (!started)
            g_debug ("Failed to start seat: %s", seat_name);
    }

    g_free (config_section);
    g_object_unref (seat);
  
    return started;
}

static void
remove_login1_seat (Login1Seat *login1_seat)
{
    Seat *seat;

    seat = display_manager_get_seat (display_manager, login1_seat_get_id (login1_seat));
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
        Seat *seat;

        /* Wait for existing seat to stop or ignore if we already have a valid seat */
        seat = display_manager_get_seat (display_manager, login1_seat_get_id (login1_seat));
        if (seat)
        {
            if (seat_get_is_stopping (seat))
                g_signal_connect (seat, "stopped", G_CALLBACK (seat_stopped_cb), login1_seat);
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
login1_service_seat_added_cb (Login1Service *service, Login1Seat *login1_seat)
{
    if (login1_seat_get_can_graphical (login1_seat))
        g_debug ("Seat %s added from logind", login1_seat_get_id (login1_seat));
    else
        g_debug ("Seat %s added from logind without graphical output", login1_seat_get_id (login1_seat));

    if (config_get_boolean (config_get_instance (), "LightDM", "logind-check-graphical"))
        g_signal_connect (login1_seat, "can-graphical-changed", G_CALLBACK (login1_can_graphical_changed_cb), NULL);
    update_login1_seat (login1_seat);
}

static void
login1_service_seat_removed_cb (Login1Service *service, Login1Seat *login1_seat)
{
    g_debug ("Seat %s removed from logind", login1_seat_get_id (login1_seat));
    g_signal_handlers_disconnect_matched (login1_seat, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, login1_can_graphical_changed_cb, NULL);
    remove_login1_seat (login1_seat);
}

int
main (int argc, char **argv)
{
    FILE *pid_file;
    GOptionContext *option_context;
    gboolean result;
    gchar *dir;
    gboolean test_mode = FALSE;
    gchar *pid_path = "/var/run/lightdm.pid";
    gchar *log_dir = NULL;
    gchar *run_dir = NULL;
    gchar *cache_dir = NULL;
    gchar *default_log_dir = g_strdup (LOG_DIR);
    gchar *default_run_dir = g_strdup (RUN_DIR);
    gchar *default_cache_dir = g_strdup (CACHE_DIR);
    gboolean show_config = FALSE, show_version = FALSE;
    GList *link, *messages = NULL;
    gboolean connected;
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
    GError *error = NULL;

    /* Disable the SIGPIPE handler - this is a stupid Unix hangover behaviour.
     * We will handle piples / sockets being closed instead of having the whole daemon be killed...
     * http://stackoverflow.com/questions/8369506/why-does-sigpipe-exist
     */
    signal (SIGPIPE, SIG_IGN);

    /* When lightdm starts sessions it needs to run itself in a new mode */
    if (argc >= 2 && strcmp (argv[1], "--session-child") == 0)
        return session_child_run (argc, argv);

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif
    loop = g_main_loop_new (NULL, FALSE);

    messages = g_list_append (messages, g_strdup_printf ("Starting Light Display Manager %s, UID=%i PID=%i", VERSION, getuid (), getpid ()));

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

    /* Show combined configuration if user requested it */
    if (show_config)
    {
        GList *sources, *link;
        gchar **groups, *last_source, *empty_source;
        GHashTable *source_ids;
        int i;

        if (!config_load_from_standard_locations (config_get_instance (), config_path, NULL))
            return EXIT_FAILURE;

        /* Number sources */
        sources = config_get_sources (config_get_instance ());
        source_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
        last_source = "";
        for (i = 0, link = sources; link; i++, link = link->next)
        {
            gchar *path, *id;

            path = link->data;
            if (i < 26)
                id = g_strdup_printf ("%c", 'A' + i);
            else
                id = g_strdup_printf ("%d", i);
            g_hash_table_insert (source_ids, g_strdup (path), id);
            last_source = id;
        }
        empty_source = g_strdup (last_source);
        for (i = 0; empty_source[i] != '\0'; i++)
            empty_source[i] = ' ';

        /* Print out keys */
        groups = config_get_groups (config_get_instance ());
        for (i = 0; groups[i]; i++)
        {
            gchar **keys;
            int j;

            if (i != 0)
                g_printerr ("\n");
            g_printerr ("%s  [%s]\n", empty_source, groups[i]);

            keys = config_get_keys (config_get_instance (), groups[i]);
            for (j = 0; keys[j]; j++)
            {
                const gchar *source, *id;
                gchar *value;

                source = config_get_source (config_get_instance (), groups[i], keys[j]);
                id = source ? g_hash_table_lookup (source_ids, source) : empty_source;
                value = config_get_string (config_get_instance (), groups[i], keys[j]);
                g_printerr ("%s  %s=%s\n", id, keys[j], value);
                g_free (value);
            }

            g_strfreev (keys);
        }
        g_strfreev (groups);

        /* Show mapping from source number to path */
        g_printerr ("\n");
        g_printerr ("Sources:\n");
        for (link = sources; link; link = link->next)
        {
            const gchar *path = link->data;
            const gchar *source;

            source = g_hash_table_lookup (source_ids, path);
            g_printerr ("%s  %s\n", source, path);
        }

        g_hash_table_destroy (source_ids);

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
        gchar *x_server_path;

        x_server_path = g_find_program_in_path ("Xephyr");
        if (!x_server_path)
        {
            g_printerr ("Running inside an X server requires Xephyr to be installed but it cannot be found.  Please install it or update your PATH environment variable.\n");
            return EXIT_FAILURE;
        }
        g_free (x_server_path);
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
    if (!config_has_key (config_get_instance (), "SeatDefaults", "type"))
        config_set_string (config_get_instance (), "SeatDefaults", "type", "xlocal");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "pam-service"))
        config_set_string (config_get_instance (), "SeatDefaults", "pam-service", "lightdm");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "pam-autologin-service"))
        config_set_string (config_get_instance (), "SeatDefaults", "pam-autologin-service", "lightdm-autologin");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "pam-greeter-service"))
        config_set_string (config_get_instance (), "SeatDefaults", "pam-greeter-service", "lightdm-greeter");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "xserver-command"))
        config_set_string (config_get_instance (), "SeatDefaults", "xserver-command", "X");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "xserver-share"))
        config_set_boolean (config_get_instance (), "SeatDefaults", "xserver-share", TRUE);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "unity-compositor-command"))
        config_set_string (config_get_instance (), "SeatDefaults", "unity-compositor-command", "unity-system-compositor");
    if (!config_has_key (config_get_instance (), "SeatDefaults", "start-session"))
        config_set_boolean (config_get_instance (), "SeatDefaults", "start-session", TRUE);
    if (!config_has_key (config_get_instance (), "SeatDefaults", "allow-user-switching"))
        config_set_boolean (config_get_instance (), "SeatDefaults", "allow-user-switching", TRUE);
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
    if (!config_has_key (config_get_instance (), "SeatDefaults", "xmir-command"))
        config_set_string (config_get_instance (), "SeatDefaults", "xmir-command", "Xmir");
    if (!config_has_key (config_get_instance (), "LightDM", "log-directory"))
        config_set_string (config_get_instance (), "LightDM", "log-directory", default_log_dir);
    g_free (default_log_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "run-directory"))
        config_set_string (config_get_instance (), "LightDM", "run-directory", default_run_dir);
    g_free (default_run_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "cache-directory"))
        config_set_string (config_get_instance (), "LightDM", "cache-directory", default_cache_dir);
    g_free (default_cache_dir);
    if (!config_has_key (config_get_instance (), "LightDM", "sessions-directory"))
        config_set_string (config_get_instance (), "LightDM", "sessions-directory", SESSIONS_DIR);
    if (!config_has_key (config_get_instance (), "LightDM", "remote-sessions-directory"))
        config_set_string (config_get_instance (), "LightDM", "remote-sessions-directory", REMOTE_SESSIONS_DIR);
    if (!config_has_key (config_get_instance (), "LightDM", "greeters-directory"))
        config_set_string (config_get_instance (), "LightDM", "greeters-directory", GREETERS_DIR);
    if (!config_has_key (config_get_instance (), "XDMCPServer", "hostname"))
        config_set_string (config_get_instance (), "XDMCPServer", "hostname", g_get_host_name ());

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
    dir = config_get_string (config_get_instance (), "LightDM", "log-directory");
    if (g_mkdir_with_parents (dir, S_IRWXU | S_IXGRP | S_IXOTH) < 0)
        g_warning ("Failed to make log directory %s: %s", dir, strerror (errno));
    g_free (dir);
    dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
    if (g_mkdir_with_parents (dir, S_IRWXU | S_IXGRP | S_IXOTH) < 0)
        g_warning ("Failed to make run directory %s: %s", dir, strerror (errno));
    g_free (dir);
    dir = config_get_string (config_get_instance (), "LightDM", "cache-directory");
    if (g_mkdir_with_parents (dir, S_IRWXU | S_IXGRP | S_IXOTH) < 0)
        g_warning ("Failed to make cache directory %s: %s", dir, strerror (errno));
    g_free (dir);

    log_init ();

    /* Show queued messages once logging is complete */
    for (link = messages; link; link = link->next)
        g_debug ("%s", (gchar *)link->data);
    g_list_free_full (messages, g_free);

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
    g_signal_connect (display_manager, "seat-removed", G_CALLBACK (display_manager_seat_removed_cb), NULL);

    shared_data_manager_start (shared_data_manager_get_instance ());

    /* Connect to logind */
    connected = login1_service_connect (login1_service_get_instance ());

    /* Load dynamic seats from logind */
    if (config_get_boolean (config_get_instance (), "LightDM", "logind-load-seats"))
    {
        if (connected)
        {
            /* Load dynamic seats from logind */
            g_debug ("Monitoring logind for seats");

            if (config_get_boolean (config_get_instance (), "LightDM", "start-default-seat"))
            {
                g_signal_connect (login1_service_get_instance (), "seat-added", G_CALLBACK (login1_service_seat_added_cb), NULL);
                g_signal_connect (login1_service_get_instance (), "seat-removed", G_CALLBACK (login1_service_seat_removed_cb), NULL);

                for (link = login1_service_get_seats (login1_service_get_instance ()); link; link = link->next)
                {
                    Login1Seat *login1_seat = link->data;
                    if (config_get_boolean (config_get_instance (), "LightDM", "logind-check-graphical"))
                        g_signal_connect (login1_seat, "can-graphical-changed", G_CALLBACK (login1_can_graphical_changed_cb), NULL);
                    if (!update_login1_seat (login1_seat))
                        return EXIT_FAILURE;
                }
            }
        }
        else if (config_get_boolean (config_get_instance (), "LightDM", "start-default-seat"))
        {
            gchar **types;
            gchar **type;
            Seat *seat = NULL;

            g_debug ("Adding default seat");

            types = config_get_string_list (config_get_instance (), "SeatDefaults", "type");
            for (type = types; type && *type; type++)
            {
                seat = seat_new (*type);
                if (seat)
                    break;
            }
            g_strfreev (types);
            if (seat)
            {
                set_seat_properties (seat, NULL);
                seat_set_property (seat, "exit-on-failure", "true");
                if (!display_manager_add_seat (display_manager, seat))
                    return EXIT_FAILURE;
                g_object_unref (seat);
            }
            else
            {
                g_warning ("Failed to create default seat");
                return EXIT_FAILURE;
            }
        }
    }
    else
    {
        gchar **groups, **i;
        gint n_seats = 0;

        /* Load the static seat entries */
        groups = config_get_groups (config_get_instance ());
        for (i = groups; *i; i++)
        {
            gchar *config_section = *i;
            gchar **types;
            gchar **type;
            Seat *seat = NULL;
            const gchar *const seatpfx = "Seat:";

            if (!g_str_has_prefix (config_section, seatpfx))
                continue;

            g_debug ("Loading seat %s", config_section);
            types = config_get_string_list (config_get_instance (), config_section, "type");
            if (!types)
                types = config_get_string_list (config_get_instance (), "SeatDefaults", "type");
            for (type = types; type && *type; type++)
            {
                seat = seat_new (*type);
                if (seat)
                    break;
            }
            g_strfreev (types);
            if (seat)
            {
                const gsize seatpfxlen = strlen(seatpfx);
                gchar *seatname = config_section + seatpfxlen;

                seat_set_property (seat, "seat-name", seatname);

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
            gchar **types;
            gchar **type;
            Seat *seat = NULL;

            g_debug ("Adding default seat");

            types = config_get_string_list (config_get_instance (), "SeatDefaults", "type");
            for (type = types; type && *type; type++)
            {
                seat = seat_new (*type);
                if (seat)
                    break;
            }
            g_strfreev (types);
            if (seat)
            {
                set_seat_properties (seat, NULL);
                seat_set_property (seat, "exit-on-failure", "true");
                if (!display_manager_add_seat (display_manager, seat))
                    return EXIT_FAILURE;
                g_object_unref (seat);
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

    /* Clean up display manager */
    g_object_unref (display_manager);
    display_manager = NULL;

    /* Remove D-Bus interface */
    g_dbus_connection_unregister_object (bus, reg_id);
    g_bus_unown_name (bus_id);
    if (seat_bus_entries)
        g_hash_table_unref (seat_bus_entries);
    if (session_bus_entries)
        g_hash_table_unref (session_bus_entries);

    g_debug ("Exiting with return value %d", exit_code);
    return exit_code;
}
