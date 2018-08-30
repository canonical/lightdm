/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*-
 *
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <string.h>
#include <gio/gio.h>

#include "login1.h"

#define LOGIN1_SERVICE_NAME "org.freedesktop.login1"
#define LOGIN1_OBJECT_NAME "/org/freedesktop/login1"
#define LOGIN1_MANAGER_INTERFACE_NAME "org.freedesktop.login1.Manager"

enum {
    SEAT_ADDED,
    SEAT_REMOVED,
    LAST_SERVICE_SIGNAL
};
static guint service_signals[LAST_SERVICE_SIGNAL] = { 0 };

typedef struct
{
    /* Connection to bus service is running on */
    GDBusConnection *connection;

    /* TRUE if have connected to service */
    gboolean connected;

    /* Seats the service is reporting */
    GList *seats;

    /* Handle to signal subscription */
    guint signal_id;
} Login1ServicePrivate;

enum {
    CAN_GRAPHICAL_CHANGED,
    ACTIVE_SESSION_CHANGED,
    LAST_SEAT_SIGNAL
};
static guint seat_signals[LAST_SEAT_SIGNAL] = { 0 };

typedef struct
{
    /* Connection to bus seat is running on */
    GDBusConnection *connection;

    /* Seat Id */
    gchar *id;

    /* D-Bus path for this seat */
    gchar *path;

    /* Handle to signal subscription */
    guint signal_id;

    /* TRUE if can run a graphical display on this seat */
    gboolean can_graphical;

    /* TRUE if can do session switching */
    gboolean can_multi_session;
} Login1SeatPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (Login1Service, login1_service, G_TYPE_OBJECT)
G_DEFINE_TYPE_WITH_PRIVATE (Login1Seat, login1_seat, G_TYPE_OBJECT)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Login1Seat, g_object_unref)

static Login1Service *singleton = NULL;

Login1Service *
login1_service_get_instance (void)
{
    if (!singleton)
        singleton = g_object_new (LOGIN1_SERVICE_TYPE, NULL);
    return singleton;
}

static void
update_property (Login1Seat *seat, const gchar *name, GVariant *value)
{
    Login1SeatPrivate *priv = login1_seat_get_instance_private (seat);

    if (strcmp (name, "CanGraphical") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
    {
        priv->can_graphical = g_variant_get_boolean (value);
        g_signal_emit (seat, seat_signals[CAN_GRAPHICAL_CHANGED], 0);
    }
    else if (strcmp (name, "ActiveSession") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE ("(so)")))
    {
        const gchar *login1_session_id;
        g_variant_get (value, "(&so)", &login1_session_id, NULL);
        g_signal_emit (seat, seat_signals[ACTIVE_SESSION_CHANGED], 0, login1_session_id);
    }
}

static void
seat_properties_changed_cb (GDBusConnection *connection,
                            const gchar *sender_name,
                            const gchar *object_path,
                            const gchar *interface_name,
                            const gchar *signal_name,
                            GVariant *parameters,
                            gpointer user_data)
{
    Login1Seat *seat = user_data;
    Login1SeatPrivate *priv = login1_seat_get_instance_private (seat);

    g_autoptr(GVariantIter) iter = NULL;
    g_autoptr(GVariantIter) invalidated_properties = NULL;
    g_variant_get (parameters, "(sa{sv}as)", NULL, &iter, &invalidated_properties);

    const gchar *name;
    GVariant *value;
    while (g_variant_iter_loop (iter, "{&sv}", &name, &value))
        update_property (seat, name, value);

    while (g_variant_iter_loop (invalidated_properties, "&s", &name))
    {
        g_autoptr(GError) error = NULL;
        g_autoptr(GVariant) result = g_dbus_connection_call_sync (connection,
                                                                  LOGIN1_SERVICE_NAME,
                                                                  priv->path,
                                                                  "org.freedesktop.DBus.Properties",
                                                                  "Get",
                                                                  g_variant_new ("(ss)", "org.freedesktop.login1.Seat", name),
                                                                  G_VARIANT_TYPE ("(v)"),
                                                                  G_DBUS_CALL_FLAGS_NONE,
                                                                  -1,
                                                                  NULL,
                                                                  &error);
        if (error)
            g_warning ("Error updating seat property %s: %s", name, error->message);
        if (result)
        {
            g_autoptr(GVariant) v = NULL;
            g_variant_get (result, "(v)", &v);
            update_property (seat, name, v);
        }
    }
}

static Login1Seat *
add_seat (Login1Service *service, const gchar *id, const gchar *path)
{
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);

    Login1Seat *seat = g_object_new (LOGIN1_SEAT_TYPE, NULL);
    Login1SeatPrivate *s_priv = login1_seat_get_instance_private (seat);

    s_priv->connection = g_object_ref (priv->connection);
    s_priv->id = g_strdup (id);
    s_priv->path = g_strdup (path);

    s_priv->signal_id = g_dbus_connection_signal_subscribe (s_priv->connection,
                                                            LOGIN1_SERVICE_NAME,
                                                            "org.freedesktop.DBus.Properties",
                                                            "PropertiesChanged",
                                                            path,
                                                            "org.freedesktop.login1.Seat",
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            seat_properties_changed_cb,
                                                            g_object_ref (seat),
                                                            g_object_unref);

    /* Get properties for this seat */
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (s_priv->connection,
                                                              LOGIN1_SERVICE_NAME,
                                                              path,
                                                              "org.freedesktop.DBus.Properties",
                                                              "GetAll",
                                                              g_variant_new ("(s)", "org.freedesktop.login1.Seat"),
                                                              G_VARIANT_TYPE ("(a{sv})"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Failed to get seat properties: %s", error->message);
    if (result)
    {
        g_autoptr(GVariantIter) properties = NULL;
        g_variant_get (result, "(a{sv})", &properties);

        const gchar *name;
        GVariant *value;
        while (g_variant_iter_loop (properties, "{&sv}", &name, &value))
        {
            if (strcmp (name, "CanGraphical") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
                s_priv->can_graphical = g_variant_get_boolean (value);
            else if (strcmp (name, "CanMultiSession") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
                s_priv->can_multi_session = g_variant_get_boolean (value);
        }
    }

    priv->seats = g_list_append (priv->seats, seat);

    return seat;
}

static void
signal_cb (GDBusConnection *connection,
           const gchar *sender_name,
           const gchar *object_path,
           const gchar *interface_name,
           const gchar *signal_name,
           GVariant *parameters,
           gpointer user_data)
{
    Login1Service *service = user_data;
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);

    if (strcmp (signal_name, "SeatNew") == 0)
    {
        const gchar *id, *path;
        g_variant_get (parameters, "(&s&o)", &id, &path);

        Login1Seat *seat = login1_service_get_seat (service, id);
        if (!seat)
        {
            seat = add_seat (service, id, path);
            g_signal_emit (service, service_signals[SEAT_ADDED], 0, seat);
        }
    }
    else if (strcmp (signal_name, "SeatRemoved") == 0)
    {
        const gchar *id, *path;
        g_variant_get (parameters, "(&s&o)", &id, &path);

        g_autoptr(Login1Seat) seat = login1_service_get_seat (service, id);
        if (seat)
        {
            priv->seats = g_list_remove (priv->seats, seat);
            g_signal_emit (service, service_signals[SEAT_REMOVED], 0, seat);
        }
    }
}

gboolean
login1_service_connect (Login1Service *service)
{
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);

    g_return_val_if_fail (service != NULL, FALSE);

    if (priv->connected)
        return TRUE;

    g_autoptr(GError) error = NULL;
    priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    if (!priv->connection)
        return FALSE;

    priv->signal_id = g_dbus_connection_signal_subscribe (priv->connection,
                                                          LOGIN1_SERVICE_NAME,
                                                          LOGIN1_MANAGER_INTERFACE_NAME,
                                                          NULL,
                                                          LOGIN1_OBJECT_NAME,
                                                          NULL,
                                                          G_DBUS_SIGNAL_FLAGS_NONE,
                                                          signal_cb,
                                                          g_object_ref (service),
                                                          g_object_unref);

    g_autoptr(GVariant) result = g_dbus_connection_call_sync (priv->connection,
                                                              LOGIN1_SERVICE_NAME,
                                                              LOGIN1_OBJECT_NAME,
                                                              LOGIN1_MANAGER_INTERFACE_NAME,
                                                              "ListSeats",
                                                              g_variant_new ("()"),
                                                              G_VARIANT_TYPE ("(a(so))"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Failed to get list of logind seats: %s", error->message);
    if (!result)
        return FALSE;

    g_autoptr(GVariantIter) seat_iter = NULL;
    g_variant_get (result, "(a(so))", &seat_iter);

    const gchar *id, *path;
    while (g_variant_iter_loop (seat_iter, "(&s&o)", &id, &path))
        add_seat (service, id, path);

    priv->connected = TRUE;

    return TRUE;
}

gboolean
login1_service_get_is_connected (Login1Service *service)
{
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);
    g_return_val_if_fail (service != NULL, FALSE);
    return priv->connected;
}

GList *
login1_service_get_seats (Login1Service *service)
{
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);
    g_return_val_if_fail (service != NULL, NULL);
    return priv->seats;
}

Login1Seat *
login1_service_get_seat (Login1Service *service, const gchar *id)
{
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);

    g_return_val_if_fail (service != NULL, NULL);

    for (GList *link = priv->seats; link; link = link->next)
    {
        Login1Seat *seat = link->data;
        Login1SeatPrivate *s_priv = login1_seat_get_instance_private (seat);

        if (strcmp (s_priv->id, id) == 0)
            return seat;
    }

    return NULL;
}

void
login1_service_lock_session (Login1Service *service, const gchar *session_id)
{
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);

    g_return_if_fail (service != NULL);
    g_return_if_fail (session_id != NULL);

    g_debug ("Locking login1 session %s", session_id);

    if (!session_id)
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (priv->connection,
                                                              LOGIN1_SERVICE_NAME,
                                                              LOGIN1_OBJECT_NAME,
                                                              LOGIN1_MANAGER_INTERFACE_NAME,
                                                              "LockSession",
                                                              g_variant_new ("(s)", session_id),
                                                              G_VARIANT_TYPE ("()"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Error locking login1 session: %s", error->message);
}

void
login1_service_unlock_session (Login1Service *service, const gchar *session_id)
{
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);

    g_return_if_fail (service != NULL);
    g_return_if_fail (session_id != NULL);

    g_debug ("Unlocking login1 session %s", session_id);

    if (!session_id)
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (priv->connection,
                                                              LOGIN1_SERVICE_NAME,
                                                              LOGIN1_OBJECT_NAME,
                                                              LOGIN1_MANAGER_INTERFACE_NAME,
                                                              "UnlockSession",
                                                              g_variant_new ("(s)", session_id),
                                                              G_VARIANT_TYPE ("()"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Error unlocking login1 session: %s", error->message);
}

void
login1_service_activate_session (Login1Service *service, const gchar *session_id)
{
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);

    g_return_if_fail (service != NULL);
    g_return_if_fail (session_id != NULL);

    g_debug ("Activating login1 session %s", session_id);

    if (!session_id)
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (priv->connection,
                                                              LOGIN1_SERVICE_NAME,
                                                              LOGIN1_OBJECT_NAME,
                                                              LOGIN1_MANAGER_INTERFACE_NAME,
                                                              "ActivateSession",
                                                              g_variant_new ("(s)", session_id),
                                                              G_VARIANT_TYPE ("()"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Error activating login1 session: %s", error->message);
}

void
login1_service_terminate_session (Login1Service *service, const gchar *session_id)
{
    Login1ServicePrivate *priv = login1_service_get_instance_private (service);

    g_return_if_fail (service != NULL);
    g_return_if_fail (session_id != NULL);

    g_debug ("Terminating login1 session %s", session_id);

    if (!session_id)
        return;

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (priv->connection,
                                                              LOGIN1_SERVICE_NAME,
                                                              LOGIN1_OBJECT_NAME,
                                                              LOGIN1_MANAGER_INTERFACE_NAME,
                                                              "TerminateSession",
                                                              g_variant_new ("(s)", session_id),
                                                              G_VARIANT_TYPE ("()"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Error terminating login1 session: %s", error->message);
}

static void
login1_service_init (Login1Service *service)
{
}

static void
login1_service_finalize (GObject *object)
{
    Login1Service *self = LOGIN1_SERVICE (object);
    Login1ServicePrivate *priv = login1_service_get_instance_private (self);

    g_list_free_full (priv->seats, g_object_unref);
    g_dbus_connection_signal_unsubscribe (priv->connection, priv->signal_id);
    g_clear_object (&priv->connection);

    G_OBJECT_CLASS (login1_service_parent_class)->finalize (object);
}

static void
login1_service_class_init (Login1ServiceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = login1_service_finalize;

    service_signals[SEAT_ADDED] =
        g_signal_new (LOGIN1_SERVICE_SIGNAL_SEAT_ADDED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (Login1ServiceClass, seat_added),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LOGIN1_SEAT_TYPE);
    service_signals[SEAT_REMOVED] =
        g_signal_new (LOGIN1_SERVICE_SIGNAL_SEAT_REMOVED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (Login1ServiceClass, seat_removed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LOGIN1_SEAT_TYPE);
}

const gchar *
login1_seat_get_id (Login1Seat *seat)
{
    Login1SeatPrivate *priv = login1_seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, NULL);
    return priv->id;
}

gboolean
login1_seat_get_can_graphical (Login1Seat *seat)
{
    Login1SeatPrivate *priv = login1_seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, FALSE);
    return priv->can_graphical;
}

gboolean
login1_seat_get_can_multi_session (Login1Seat *seat)
{
    Login1SeatPrivate *priv = login1_seat_get_instance_private (seat);
    g_return_val_if_fail (seat != NULL, FALSE);
    return priv->can_multi_session;
}

static void
login1_seat_init (Login1Seat *seat)
{
}

static void
login1_seat_finalize (GObject *object)
{
    Login1Seat *self = LOGIN1_SEAT (object);
    Login1SeatPrivate *priv = login1_seat_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->path, g_free);
    g_dbus_connection_signal_unsubscribe (priv->connection, priv->signal_id);
    g_clear_object (&priv->connection);

    G_OBJECT_CLASS (login1_seat_parent_class)->finalize (object);
}

static void
login1_seat_class_init (Login1SeatClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = login1_seat_finalize;

    seat_signals[CAN_GRAPHICAL_CHANGED] =
        g_signal_new (LOGIN1_SEAT_SIGNAL_CAN_GRAPHICAL_CHANGED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (Login1SeatClass, can_graphical_changed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    seat_signals[ACTIVE_SESSION_CHANGED] =
        g_signal_new (LOGIN1_SIGNAL_ACTIVE_SESION_CHANGED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (Login1SeatClass, active_session_changed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
}
