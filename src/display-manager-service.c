/*
 * Copyright (C) 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <config.h>

#include "display-manager-service.h"

enum {
    READY,
    ADD_XLOCAL_SEAT,
    NAME_LOST,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    /* Display manager being exposed on D-Bus */
    DisplayManager *manager;

    /* Bus connected to */
    GDBusConnection *bus;

    /* Handle for D-Bus name */
    guint bus_id;

    /* Handle for display manager D-Bus object */
    guint reg_id;

    /* D-Bus interface information */
    GDBusNodeInfo *seat_info;
    GDBusNodeInfo *session_info;

    /* Next index to use for seat / session entries */
    guint seat_index;
    guint session_index;

    /* Bus entries for seats / session */
    GHashTable *seat_bus_entries;
    GHashTable *session_bus_entries;
} DisplayManagerServicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DisplayManagerService, display_manager_service, G_TYPE_OBJECT)

typedef struct
{
    DisplayManagerService *service;
    Seat *seat;
    gchar *path;
    guint bus_id;
} SeatBusEntry;
typedef struct
{
    DisplayManagerService *service;
    Session *session;
    gchar *path;
    gchar *seat_path;
    guint bus_id;
} SessionBusEntry;

#define LIGHTDM_BUS_NAME "org.freedesktop.DisplayManager"

DisplayManagerService *
display_manager_service_new (DisplayManager *manager)
{
    DisplayManagerService *service = g_object_new (DISPLAY_MANAGER_SERVICE_TYPE, NULL);
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    priv->manager = g_object_ref (manager);

    return service;
}

static SeatBusEntry *
seat_bus_entry_new (DisplayManagerService *service, Seat *seat, const gchar *path)
{
    SeatBusEntry *entry = g_malloc0 (sizeof (SeatBusEntry));
    entry->service = service;
    entry->seat = seat;
    entry->path = g_strdup (path);

    return entry;
}

static SessionBusEntry *
session_bus_entry_new (DisplayManagerService *service, Session *session, const gchar *path, const gchar *seat_path)
{
    SessionBusEntry *entry = g_malloc0 (sizeof (SessionBusEntry));
    entry->service = service;
    entry->session = session;
    entry->path = g_strdup (path);
    entry->seat_path = g_strdup (seat_path);

    return entry;
}

static void
emit_object_value_changed (GDBusConnection *bus, const gchar *path, const gchar *interface_name, const gchar *property_name, GVariant *property_value)
{
    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
    g_variant_builder_add (&builder, "{sv}", property_name, property_value);

    g_autoptr(GError) error = NULL;
    if (!g_dbus_connection_emit_signal (bus,
                                        NULL,
                                        path,
                                        "org.freedesktop.DBus.Properties",
                                        "PropertiesChanged",
                                        g_variant_new ("(sa{sv}as)", interface_name, &builder, NULL),
                                        &error))
        g_warning ("Failed to emit PropertiesChanged signal: %s", error->message);
}

static void
emit_object_signal (GDBusConnection *bus, const gchar *path, const gchar *signal_name, const gchar *object_path)
{
    g_autoptr(GError) error = NULL;
    if (!g_dbus_connection_emit_signal (bus,
                                        NULL,
                                        path,
                                        "org.freedesktop.DisplayManager",
                                        signal_name,
                                        g_variant_new ("(o)", object_path),
                                        &error))
        g_warning ("Failed to emit %s signal on %s: %s", signal_name, path, error->message);
}

static void
seat_bus_entry_free (gpointer data)
{
    SeatBusEntry *entry = data;

    g_free (entry->path);
    g_free (entry);
}

static void
session_bus_entry_free (gpointer data)
{
    SessionBusEntry *entry = data;

    g_free (entry->path);
    g_free (entry->seat_path);
    g_free (entry);
}

static GVariant *
get_seat_list (DisplayManagerService *service)
{
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("ao"));

    GHashTableIter iter;
    g_hash_table_iter_init (&iter, priv->seat_bus_entries);
    gpointer value;
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        SeatBusEntry *entry = value;
        g_variant_builder_add_value (&builder, g_variant_new_object_path (entry->path));
    }

    return g_variant_builder_end (&builder);
}

static GVariant *
get_session_list (DisplayManagerService *service, const gchar *seat_path)
{
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("ao"));

    GHashTableIter iter;
    g_hash_table_iter_init (&iter, priv->session_bus_entries);
    gpointer value;
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        SessionBusEntry *entry = value;
        if (seat_path == NULL || g_strcmp0 (entry->seat_path, seat_path) == 0)
            g_variant_builder_add_value (&builder, g_variant_new_object_path (entry->path));
    }

    return g_variant_builder_end (&builder);
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
    DisplayManagerService *service = user_data;

    if (g_strcmp0 (property_name, "Seats") == 0)
        return get_seat_list (service);
    else if (g_strcmp0 (property_name, "Sessions") == 0)
        return get_session_list (service, NULL);

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
    DisplayManagerService *service = user_data;
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    if (g_strcmp0 (method_name, "AddSeat") == 0)
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "AddSeat is deprecated");
    else if (g_strcmp0 (method_name, "AddLocalXSeat") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(i)")))
        {
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");
            return;
        }

        gint display_number;
        g_variant_get (parameters, "(i)", &display_number);

        g_autoptr(Seat) seat = NULL;
        g_signal_emit (service, signals[ADD_XLOCAL_SEAT], 0, display_number, &seat);

        if (!seat)
        {
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Unable to create local X seat");
            return;
        }
        SeatBusEntry *entry = g_hash_table_lookup (priv->seat_bus_entries, seat);
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", entry->path));
    }
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
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
    SeatBusEntry *entry = user_data;

    if (g_strcmp0 (property_name, "CanSwitch") == 0)
        return g_variant_new_boolean (seat_get_can_switch (entry->seat));
    if (g_strcmp0 (property_name, "HasGuestAccount") == 0)
        return g_variant_new_boolean (seat_get_allow_guest (entry->seat));
    else if (g_strcmp0 (property_name, "Sessions") == 0)
        return get_session_list (entry->service, entry->path);

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
    SeatBusEntry *entry = user_data;

    if (g_strcmp0 (method_name, "SwitchToGreeter") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("()")))
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");

        if (seat_switch_to_greeter (entry->seat))
            g_dbus_method_invocation_return_value (invocation, NULL);
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to switch to greeter");
    }
    else if (g_strcmp0 (method_name, "SwitchToUser") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(ss)")))
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");

        const gchar *username, *session_name;
        g_variant_get (parameters, "(&s&s)", &username, &session_name);
        if (g_strcmp0 (session_name, "") == 0)
            session_name = NULL;

        if (seat_switch_to_user (entry->seat, username, session_name))
            g_dbus_method_invocation_return_value (invocation, NULL);
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to switch to user");
    }
    else if (g_strcmp0 (method_name, "SwitchToGuest") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");

        const gchar *session_name;
        g_variant_get (parameters, "(&s)", &session_name);
        if (g_strcmp0 (session_name, "") == 0)
            session_name = NULL;

        if (seat_switch_to_guest (entry->seat, session_name))
            g_dbus_method_invocation_return_value (invocation, NULL);
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to switch to guest");
    }
    else if (g_strcmp0 (method_name, "Lock") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("()")))
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");

        /* FIXME: Should only allow locks if have a session on this seat */
        if (seat_lock (entry->seat, NULL))
            g_dbus_method_invocation_return_value (invocation, NULL);
        else// FIXME: Need to make proper error
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Failed to lock seat");
    }
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
}

static Seat *
get_seat_for_session (DisplayManagerService *service, Session *session)
{
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    for (GList *seat_link = display_manager_get_seats (priv->manager); seat_link; seat_link = seat_link->next)
    {
        Seat *seat = seat_link->data;

        for (GList *session_link = seat_get_sessions (seat); session_link; session_link = session_link->next)
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
    SessionBusEntry *entry = user_data;

    if (g_strcmp0 (property_name, "Seat") == 0)
        return g_variant_new_object_path (entry->seat_path);
    else if (g_strcmp0 (property_name, "UserName") == 0)
        return g_variant_new_string (session_get_username (entry->session));

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
    SessionBusEntry *entry = user_data;

    if (g_strcmp0 (method_name, "Lock") == 0)
    {
        if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("()")))
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Invalid arguments");

        Seat *seat = get_seat_for_session (entry->service, entry->session);
        /* FIXME: Should only allow locks if have a session on this seat */
        seat_lock (seat, session_get_username (entry->session));
        g_dbus_method_invocation_return_value (invocation, NULL);
    }
    else
        g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
}

static void
running_user_session_cb (Seat *seat, Session *session, DisplayManagerService *service)
{
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    /* Set environment variables when session runs */
    SeatBusEntry *seat_entry = g_hash_table_lookup (priv->seat_bus_entries, seat);
    session_set_env (session, "XDG_SEAT_PATH", seat_entry->path);
    g_autofree gchar *path = g_strdup_printf ("/org/freedesktop/DisplayManager/Session%d", priv->session_index);
    priv->session_index++;
    session_set_env (session, "XDG_SESSION_PATH", path);
    g_object_set_data_full (G_OBJECT (session), "XDG_SESSION_PATH", g_steal_pointer (&path), g_free);

    SessionBusEntry *session_entry = session_bus_entry_new (service, session, g_object_get_data (G_OBJECT (session), "XDG_SESSION_PATH"), seat_entry ? seat_entry->path : NULL);
    g_hash_table_insert (priv->session_bus_entries, g_object_ref (session), session_entry);

    g_debug ("Registering session with bus path %s", session_entry->path);

    static const GDBusInterfaceVTable session_vtable =
    {
        handle_session_call,
        handle_session_get_property
    };
    g_autoptr(GError) error = NULL;
    session_entry->bus_id = g_dbus_connection_register_object (priv->bus,
                                                               session_entry->path,
                                                               priv->session_info->interfaces[0],
                                                               &session_vtable,
                                                               session_entry, NULL,
                                                               &error);
    if (session_entry->bus_id == 0)
        g_warning ("Failed to register user session: %s", error->message);

    emit_object_value_changed (priv->bus, "/org/freedesktop/DisplayManager", "org.freedesktop.DisplayManager", "Sessions", get_session_list (service, NULL));
    emit_object_signal (priv->bus, "/org/freedesktop/DisplayManager", "SessionAdded", session_entry->path);

    emit_object_value_changed (priv->bus, seat_entry->path, "org.freedesktop.DisplayManager.Seat", "Sessions", get_session_list (service, session_entry->seat_path));
    emit_object_signal (priv->bus, seat_entry->path, "SessionAdded", session_entry->path);
}

static void
session_removed_cb (Seat *seat, Session *session, DisplayManagerService *service)
{
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    g_signal_handlers_disconnect_matched (session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);

    SessionBusEntry *entry = g_hash_table_lookup (priv->session_bus_entries, session);
    g_autofree gchar *seat_path = NULL;
    if (entry)
    {
        g_dbus_connection_unregister_object (priv->bus, entry->bus_id);
        emit_object_signal (priv->bus, "/org/freedesktop/DisplayManager", "SessionRemoved", entry->path);
        emit_object_signal (priv->bus, entry->seat_path, "SessionRemoved", entry->path);
        seat_path = g_strdup (entry->seat_path);
    }

    g_hash_table_remove (priv->session_bus_entries, session);

    if (seat_path)
    {
        emit_object_value_changed (priv->bus, "/org/freedesktop/DisplayManager", "org.freedesktop.DisplayManager", "Sessions", get_session_list (service, NULL));
        emit_object_value_changed (priv->bus, seat_path, "org.freedesktop.DisplayManager.Seat", "Sessions", get_session_list (service, seat_path));
    }
}

static void
seat_added_cb (DisplayManager *display_manager, Seat *seat, DisplayManagerService *service)
{
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    g_autofree gchar *path = g_strdup_printf ("/org/freedesktop/DisplayManager/Seat%d", priv->seat_index);
    priv->seat_index++;

    SeatBusEntry *entry = seat_bus_entry_new (service, seat, path);
    g_hash_table_insert (priv->seat_bus_entries, g_object_ref (seat), entry);

    g_debug ("Registering seat with bus path %s", entry->path);

    static const GDBusInterfaceVTable seat_vtable =
    {
        handle_seat_call,
        handle_seat_get_property
    };
    g_autoptr(GError) error = NULL;
    entry->bus_id = g_dbus_connection_register_object (priv->bus,
                                                       entry->path,
                                                       priv->seat_info->interfaces[0],
                                                       &seat_vtable,
                                                       entry, NULL,
                                                       &error);
    if (entry->bus_id == 0)
        g_warning ("Failed to register seat: %s", error->message);

    emit_object_value_changed (priv->bus, "/org/freedesktop/DisplayManager", "org.freedesktop.DisplayManager", "Seats", get_seat_list (service));
    emit_object_signal (priv->bus, "/org/freedesktop/DisplayManager", "SeatAdded", entry->path);

    g_signal_connect (seat, SEAT_SIGNAL_RUNNING_USER_SESSION, G_CALLBACK (running_user_session_cb), service);
    g_signal_connect (seat, SEAT_SIGNAL_SESSION_REMOVED, G_CALLBACK (session_removed_cb), service);
}

static void
seat_removed_cb (DisplayManager *display_manager, Seat *seat, DisplayManagerService *service)
{
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    SeatBusEntry *entry = g_hash_table_lookup (priv->seat_bus_entries, seat);
    if (entry)
    {
        g_dbus_connection_unregister_object (priv->bus, entry->bus_id);
        emit_object_signal (priv->bus, "/org/freedesktop/DisplayManager", "SeatRemoved", entry->path);
    }

    g_hash_table_remove (priv->seat_bus_entries, seat);

    emit_object_value_changed (priv->bus, "/org/freedesktop/DisplayManager", "org.freedesktop.DisplayManager", "Seats", get_seat_list (service));
}

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    DisplayManagerService *service = user_data;
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    g_debug ("Acquired bus name %s", name);

    priv->bus = g_object_ref (connection);

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
    GDBusNodeInfo *display_manager_info = g_dbus_node_info_new_for_xml (display_manager_interface, NULL);
    g_assert (display_manager_info != NULL);

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
        "    <signal name='SessionAdded'>"
        "      <arg name='session' type='o'/>"
        "    </signal>"
        "    <signal name='SessionRemoved'>"
        "      <arg name='session' type='o'/>"
        "    </signal>"
        "  </interface>"
        "</node>";
    priv->seat_info = g_dbus_node_info_new_for_xml (seat_interface, NULL);
    g_assert (priv->seat_info != NULL);

    const gchar *session_interface =
        "<node>"
        "  <interface name='org.freedesktop.DisplayManager.Session'>"
        "    <property name='Seat' type='o' access='read'/>"
        "    <property name='UserName' type='s' access='read'/>"
        "    <method name='Lock'/>"
        "  </interface>"
        "</node>";
    priv->session_info = g_dbus_node_info_new_for_xml (session_interface, NULL);
    g_assert (priv->session_info != NULL);

    static const GDBusInterfaceVTable display_manager_vtable =
    {
        handle_display_manager_call,
        handle_display_manager_get_property
    };
    g_autoptr(GError) error = NULL;
    priv->reg_id = g_dbus_connection_register_object (connection,
                                                      "/org/freedesktop/DisplayManager",
                                                      display_manager_info->interfaces[0],
                                                      &display_manager_vtable,
                                                      service, NULL,
                                                      &error);
    if (priv->reg_id == 0)
        g_warning ("Failed to register display manager: %s", error->message);
    g_dbus_node_info_unref (display_manager_info);

    /* Add objects for existing seats and listen to new ones */
    g_signal_connect (priv->manager, DISPLAY_MANAGER_SIGNAL_SEAT_ADDED, G_CALLBACK (seat_added_cb), service);
    g_signal_connect (priv->manager, DISPLAY_MANAGER_SIGNAL_SEAT_REMOVED, G_CALLBACK (seat_removed_cb), service);
    for (GList *link = display_manager_get_seats (priv->manager); link; link = link->next)
        seat_added_cb (priv->manager, (Seat *) link->data, service);

    g_signal_emit (service, signals[READY], 0);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
    DisplayManagerService *service = user_data;

    if (connection)
        g_printerr ("Failed to use bus name " LIGHTDM_BUS_NAME ", do you have appropriate permissions?\n");
    else
        g_printerr ("Failed to get D-Bus connection\n");

    g_signal_emit (service, signals[NAME_LOST], 0);
}

void
display_manager_service_start (DisplayManagerService *service)
{
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);

    g_return_if_fail (service != NULL);

    g_debug ("Using D-Bus name %s", LIGHTDM_BUS_NAME);
    priv->bus_id = g_bus_own_name (getuid () == 0 ? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION,
                                   LIGHTDM_BUS_NAME,
                                   G_BUS_NAME_OWNER_FLAGS_NONE,
                                   bus_acquired_cb,
                                   NULL,
                                   name_lost_cb,
                                   service,
                                   NULL);
}

static void
display_manager_service_init (DisplayManagerService *service)
{
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (service);
    priv->seat_bus_entries = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, seat_bus_entry_free);
    priv->session_bus_entries = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, session_bus_entry_free);
}

static void
display_manager_service_finalize (GObject *object)
{
    DisplayManagerService *self = DISPLAY_MANAGER_SERVICE (object);
    DisplayManagerServicePrivate *priv = display_manager_service_get_instance_private (self);

    g_dbus_connection_unregister_object (priv->bus, priv->reg_id);
    g_bus_unown_name (priv->bus_id);
    if (priv->seat_info)
        g_dbus_node_info_unref (priv->seat_info);
    if (priv->session_info)
        g_dbus_node_info_unref (priv->session_info);
    g_hash_table_unref (priv->seat_bus_entries);
    g_hash_table_unref (priv->session_bus_entries);
    g_object_unref (priv->bus);
    g_clear_object (&priv->manager);

    G_OBJECT_CLASS (display_manager_service_parent_class)->finalize (object);
}

static void
display_manager_service_class_init (DisplayManagerServiceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = display_manager_service_finalize;

    signals[READY] =
        g_signal_new (DISPLAY_MANAGER_SERVICE_SIGNAL_READY,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerServiceClass, ready),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[ADD_XLOCAL_SEAT] =
        g_signal_new (DISPLAY_MANAGER_SERVICE_SIGNAL_ADD_XLOCAL_SEAT,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerServiceClass, add_xlocal_seat),
                      g_signal_accumulator_first_wins,
                      NULL,
                      NULL,
                      SEAT_TYPE, 1, G_TYPE_INT);

    signals[NAME_LOST] =
        g_signal_new (DISPLAY_MANAGER_SERVICE_SIGNAL_NAME_LOST,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerServiceClass, name_lost),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}
