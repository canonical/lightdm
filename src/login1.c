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

enum {
    SEAT_ADDED,
    SEAT_REMOVED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct Login1ServicePrivate
{
    /* Connection to bus service is running on */
    GDBusConnection *connection;

    /* Seats the service is reporting */
    GList *seats;

    /* Handle to signal subscription */
    guint signal_id;
};

struct Login1SeatPrivate
{
    /* Seat Id */
    gchar *id;

    /* D-Bus path for this seat */
    gchar *path;
};

G_DEFINE_TYPE (Login1Service, login1_service, G_TYPE_OBJECT);
G_DEFINE_TYPE (Login1Seat, login1_seat, G_TYPE_OBJECT);

static Login1Service *singleton = NULL;

static gboolean
check_login1 (void)
{
    GDBusProxy *proxy;
    gchar *owner;
    gboolean success;

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                           NULL,
                                           LOGIN1_SERVICE_NAME,
                                           "/org/freedesktop/login1",
                                           "org.freedesktop.login1.Manager",
                                           NULL,
                                           NULL);
    if (!proxy)
        return FALSE;

    owner = g_dbus_proxy_get_name_owner (proxy);
    g_object_unref (proxy);

    success = (owner != NULL);
    g_free (owner);

    return success;
}

gboolean
login1_is_running (void)
{
    static gboolean have_checked = FALSE;
    static gboolean is_running = FALSE;

    if (!have_checked)
    {
        have_checked = TRUE;
        is_running = check_login1 ();
    }

    return is_running;
}

gchar *
login1_get_session_id (void)
{
    GDBusConnection *bus;
    GVariant *result;
    gchar *session_path;
    GError *error = NULL;

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return NULL;
    result = g_dbus_connection_call_sync (bus,
                                          LOGIN1_SERVICE_NAME,
                                          "/org/freedesktop/login1",
                                          "org.freedesktop.login1.Manager",
                                          "GetSessionByPID",
                                          g_variant_new ("(u)", getpid()),
                                          G_VARIANT_TYPE ("(o)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    g_object_unref (bus);

    if (error)
        g_warning ("Failed to open login1 session: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return NULL;

    g_variant_get (result, "(o)", &session_path);
    g_variant_unref (result);
    g_debug ("Got login1 session id: %s", session_path);

    return session_path;
}

void
login1_lock_session (const gchar *session_path)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (session_path != NULL);

    g_debug ("Locking login1 session %s", session_path);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              LOGIN1_SERVICE_NAME,
                                              session_path,
                                              "org.freedesktop.login1.Session",
                                              "Lock",
                                              g_variant_new ("()"),
                                              G_VARIANT_TYPE ("()"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error locking login1 session: %s", error->message);
        g_clear_error (&error);
        if (result)
            g_variant_unref (result);
    }
    g_object_unref (bus);
}

void
login1_unlock_session (const gchar *session_path)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (session_path != NULL);

    g_debug ("Unlocking login1 session %s", session_path);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              LOGIN1_SERVICE_NAME,
                                              session_path,
                                              "org.freedesktop.login1.Session",
                                              "Unlock",
                                              g_variant_new ("()"),
                                              G_VARIANT_TYPE ("()"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error unlocking login1 session: %s", error->message);
        g_clear_error (&error);
        if (result)
            g_variant_unref (result);
    }
    g_object_unref (bus);
}

void
login1_activate_session (const gchar *session_path)
{
    GDBusConnection *bus;
    GError *error = NULL;

    g_return_if_fail (session_path != NULL);

    g_debug ("Activating login1 session %s", session_path);

    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!bus)
        return;

    if (session_path)
    {
        GVariant *result;

        result = g_dbus_connection_call_sync (bus,
                                              LOGIN1_SERVICE_NAME,
                                              session_path,
                                              "org.freedesktop.login1.Session",
                                              "Activate",
                                              g_variant_new ("()"),
                                              G_VARIANT_TYPE ("()"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);
        if (error)
            g_warning ("Error activating login1 session: %s", error->message);
        g_clear_error (&error);
        if (result)
            g_variant_unref (result);
    }
    g_object_unref (bus);
}

Login1Service *
login1_service_get_instance (void)
{
    if (!singleton)
        singleton = g_object_new (LOGIN1_SERVICE_TYPE, NULL);
    return singleton;
}

static Login1Seat *
add_seat (Login1Service *service, const gchar *id, const gchar *path)
{
    Login1Seat *seat;

    seat = g_object_new (LOGIN1_SEAT_TYPE, NULL);
    seat->priv->id = g_strdup (id);
    seat->priv->path = g_strdup (path);
    service->priv->seats = g_list_append (service->priv->seats, seat);

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

    if (strcmp (signal_name, "SeatNew") == 0)
    {
        const gchar *id, *path;
        Login1Seat *seat;

        g_variant_get (parameters, "(&s&o)", &id, &path);
        seat = login1_service_get_seat (service, id);
        if (!seat)
        {
            seat = add_seat (service, id, path);
            g_signal_emit (service, signals[SEAT_ADDED], 0, seat);
        }
    }
    else if (strcmp (signal_name, "SeatRemoved") == 0)
    {
        const gchar *id, *path;
        Login1Seat *seat;

        g_variant_get (parameters, "(&s&o)", &id, &path);
        seat = login1_service_get_seat (service, id);
        if (seat)
        {
            service->priv->seats = g_list_remove (service->priv->seats, seat);            
            g_signal_emit (service, signals[SEAT_REMOVED], 0, seat);
            g_object_unref (seat);
        }                        
    } 
}

gboolean
login1_service_connect (Login1Service *service)
{
    GVariant *result;
    GVariantIter *seat_iter;
    const gchar *id, *path;
    GError *error = NULL;

    g_return_val_if_fail (service != NULL, FALSE);

    service->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_warning ("Failed to get system bus: %s", error->message);
    g_clear_error (&error);
    if (!service->priv->connection)
        return FALSE;

    service->priv->signal_id = g_dbus_connection_signal_subscribe (service->priv->connection,
                                                                   LOGIN1_SERVICE_NAME,
                                                                   "org.freedesktop.login1.Manager",
                                                                   NULL,
                                                                   "/org/freedesktop/login1",
                                                                   NULL,
                                                                   G_DBUS_SIGNAL_FLAGS_NONE,
                                                                   signal_cb,
                                                                   service,
                                                                   NULL);

    result = g_dbus_connection_call_sync (service->priv->connection,
                                          LOGIN1_SERVICE_NAME,
                                          "/org/freedesktop/login1",
                                          "org.freedesktop.login1.Manager",
                                          "ListSeats",
                                          g_variant_new ("()"),
                                          G_VARIANT_TYPE ("(a(so))"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
    if (error)
        g_warning ("Failed to get list of logind seats: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    g_variant_get (result, "(a(so))", &seat_iter);
    while (g_variant_iter_loop (seat_iter, "(&s&o)", &id, &path))
        add_seat (service, id, path);
    g_variant_iter_free (seat_iter);
    g_variant_unref (result);

    return TRUE;
}

GList *
login1_service_get_seats (Login1Service *service)
{
    g_return_val_if_fail (service != NULL, NULL);
    return service->priv->seats;
}

Login1Seat *
login1_service_get_seat (Login1Service *service, const gchar *id)
{
    GList *link;

    g_return_val_if_fail (service != NULL, NULL);

    for (link = service->priv->seats; link; link = link->next)
    {
        Login1Seat *seat = link->data;
        if (strcmp (seat->priv->id, id) == 0)
            return seat;
    }

    return NULL;
}

static void
login1_service_init (Login1Service *service)
{
    service->priv = G_TYPE_INSTANCE_GET_PRIVATE (service, LOGIN1_SERVICE_TYPE, Login1ServicePrivate);
}

static void
login1_service_finalize (GObject *object)
{
    Login1Service *self = LOGIN1_SERVICE (object);

    g_list_free_full (self->priv->seats, g_object_unref);
    g_dbus_connection_signal_unsubscribe (self->priv->connection, self->priv->signal_id);
    g_object_unref (self->priv->connection);

    G_OBJECT_CLASS (login1_service_parent_class)->finalize (object);
}

static void
login1_service_class_init (Login1ServiceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = login1_service_finalize;

    g_type_class_add_private (klass, sizeof (Login1ServicePrivate));

    signals[SEAT_ADDED] =
        g_signal_new ("seat-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (Login1ServiceClass, seat_added),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LOGIN1_SEAT_TYPE);
    signals[SEAT_REMOVED] =
        g_signal_new ("seat-removed",
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
    g_return_val_if_fail (seat != NULL, NULL);
    return seat->priv->id;
}

static void
login1_seat_init (Login1Seat *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, LOGIN1_SEAT_TYPE, Login1SeatPrivate);
}

static void
login1_seat_finalize (GObject *object)
{
    Login1Seat *self = LOGIN1_SEAT (object);

    g_free (self->priv->id);
    g_free (self->priv->path);

    G_OBJECT_CLASS (login1_seat_parent_class)->finalize (object);
}

static void
login1_seat_class_init (Login1SeatClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = login1_seat_finalize;

    g_type_class_add_private (klass, sizeof (Login1SeatPrivate));
}
