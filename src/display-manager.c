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
#include <string.h>
#include <sys/wait.h>

#include "display-manager.h"
#include "configuration.h"
#include "seat-xlocal.h"
#include "seat-xremote.h"
#include "seat-unity.h"
#include "plymouth.h"

enum {
    SEAT_ADDED,
    SEAT_REMOVED,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct DisplayManagerPrivate
{
    /* The seats available */
    GList *seats;

    /* TRUE if stopping the display manager (waiting for seats to stop) */
    gboolean stopping;

    /* TRUE if stopped */
    gboolean stopped;
};

G_DEFINE_TYPE (DisplayManager, display_manager, G_TYPE_OBJECT);

DisplayManager *
display_manager_new (void)
{
    return g_object_new (DISPLAY_MANAGER_TYPE, NULL);
}

GList *
display_manager_get_seats (DisplayManager *manager)
{
    return manager->priv->seats;
}

Seat *
display_manager_get_seat (DisplayManager *manager, const gchar *name)
{
    GList *link;

    for (link = manager->priv->seats; link; link = link->next)
    {
        Seat *seat = link->data;

        if (strcmp (seat_get_name (seat), name) == 0)
            return seat;
    }

    return NULL;
}

static void
check_stopped (DisplayManager *manager)
{
    if (manager->priv->stopping &&
        !manager->priv->stopped &&
        g_list_length (manager->priv->seats) == 0)
    {
        manager->priv->stopped = TRUE;
        g_debug ("Display manager stopped");
        g_signal_emit (manager, signals[STOPPED], 0);
    }
}

static void
seat_stopped_cb (Seat *seat, DisplayManager *manager)
{
    manager->priv->seats = g_list_remove (manager->priv->seats, seat);
    g_signal_handlers_disconnect_matched (seat, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, manager);

    if (!manager->priv->stopping)
        g_signal_emit (manager, signals[SEAT_REMOVED], 0, seat);

    g_object_unref (seat);

    check_stopped (manager);
}

gboolean
display_manager_add_seat (DisplayManager *manager, Seat *seat)
{
    gboolean result;

    g_return_val_if_fail (!manager->priv->stopping, FALSE);

    result = seat_start (SEAT (seat));
    if (!result)
        return FALSE;

    manager->priv->seats = g_list_append (manager->priv->seats, g_object_ref (seat));
    g_signal_connect (seat, "stopped", G_CALLBACK (seat_stopped_cb), manager);
    g_signal_emit (manager, signals[SEAT_ADDED], 0, seat);

    return TRUE;
}

void
display_manager_start (DisplayManager *manager)
{
    g_return_if_fail (manager != NULL);

    /* Disable Plymouth if no X servers are replacing it */
    if (plymouth_get_is_active ())
    {
        g_debug ("Stopping Plymouth, no displays replace it");      
        plymouth_quit (FALSE);
    }
}

void
display_manager_stop (DisplayManager *manager)
{
    GList *seats, *link;

    g_return_if_fail (manager != NULL);

    if (manager->priv->stopping)
        return;

    g_debug ("Stopping display manager");

    manager->priv->stopping = TRUE;

    /* Stop all the seats. Copy the list as it might be modified if a seat stops during this loop */
    seats = g_list_copy (manager->priv->seats);
    for (link = seats; link; link = link->next)
    {
        Seat *seat = link->data;
        seat_stop (seat);
    }
    g_list_free (seats);

    check_stopped (manager);
}
  
static void
display_manager_init (DisplayManager *manager)
{
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, DISPLAY_MANAGER_TYPE, DisplayManagerPrivate);

    /* Load the seat modules */
    seat_register_module ("xlocal", SEAT_XLOCAL_TYPE);
    seat_register_module ("xremote", SEAT_XREMOTE_TYPE);
    seat_register_module ("unity", SEAT_UNITY_TYPE);
}

static void
display_manager_finalize (GObject *object)
{
    DisplayManager *self;
    GList *link;

    self = DISPLAY_MANAGER (object);

    for (link = self->priv->seats; link; link = link->next)
    {
        Seat *seat = link->data;
        g_signal_handlers_disconnect_matched (seat, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    }
    g_list_free_full (self->priv->seats, g_object_unref);

    G_OBJECT_CLASS (display_manager_parent_class)->finalize (object);
}

static void
display_manager_class_init (DisplayManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = display_manager_finalize;

    g_type_class_add_private (klass, sizeof (DisplayManagerPrivate));

    signals[SEAT_ADDED] =
        g_signal_new ("seat-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, seat_added),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SEAT_TYPE);
    signals[SEAT_REMOVED] =
        g_signal_new ("seat-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, seat_removed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SEAT_TYPE);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, stopped),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}
