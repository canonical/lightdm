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
#include "seat-local.h"
#include "seat-xremote.h"
#include "plymouth.h"

enum {
    SEAT_ADDED,
    SEAT_REMOVED,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    /* The seats available */
    GList *seats; // 管理登录的用户会话列表

    /* TRUE if stopping the display manager (waiting for seats to stop) */
    gboolean stopping; // TRUE 代表正在停止会话，等待seats停止

    /* TRUE if stopped */
    gboolean stopped; // TRUE 代表会话已停止
} DisplayManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DisplayManager, display_manager, G_TYPE_OBJECT)

/**
 * @brief display_manager_new
 * 初始化display_manager，此函数的主要作用是在C开发环境模拟单例模式。
 * @return DisplayManager *
 */
DisplayManager *display_manager_new(void)
{
    return g_object_new (DISPLAY_MANAGER_TYPE, NULL);
}

GList *display_manager_get_seats(DisplayManager *manager)
{
    DisplayManagerPrivate *priv = display_manager_get_instance_private (manager);
    return priv->seats;
}

Seat *display_manager_get_seat(DisplayManager *manager, const gchar *name)
{
    DisplayManagerPrivate *priv = display_manager_get_instance_private (manager);

    for (GList *link = priv->seats; link; link = link->next)
    {
        Seat *seat = link->data;

        if (strcmp (seat_get_name (seat), name) == 0)
            return seat;
    }

    return NULL;
}

static void check_stopped(DisplayManager *manager)
{
    DisplayManagerPrivate *priv = display_manager_get_instance_private (manager);

    if (priv->stopping &&
        !priv->stopped &&
        g_list_length (priv->seats) == 0)
    {
        priv->stopped = TRUE;
        g_debug ("Display manager stopped");
        g_signal_emit (manager, signals[STOPPED], 0);
    }
}

static void seat_stopped_cb(Seat *seat, DisplayManager *manager)
{
    DisplayManagerPrivate *priv = display_manager_get_instance_private (manager);

    priv->seats = g_list_remove (priv->seats, seat);
    g_signal_handlers_disconnect_matched (seat, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, manager);

    if (!priv->stopping)
        g_signal_emit (manager, signals[SEAT_REMOVED], 0, seat);

    g_object_unref (seat);

    check_stopped (manager);
}

/**
 * @brief display_manager_add_seat
 * 调用了 seat_start 方法，并触发了 DISPLAY_MANAGER_SIGNAL_SEAT_ADDED 信号。
 * 订阅此信号的是 dispmay-manager-service，它是 org.freedesktop.DisplayManager 这个系统 D-Bus 服务的实现。
 */
gboolean display_manager_add_seat(DisplayManager *manager, Seat *seat)
{
    DisplayManagerPrivate *priv = display_manager_get_instance_private (manager);

    g_return_val_if_fail (!priv->stopping, FALSE);

    gboolean result = seat_start (SEAT (seat));
    if (!result) // seat start failed, return false
        return FALSE;

    priv->seats = g_list_append (priv->seats, g_object_ref (seat));
    g_signal_connect (seat, SEAT_SIGNAL_STOPPED, G_CALLBACK (seat_stopped_cb), manager);
    g_signal_emit (manager, signals[SEAT_ADDED], 0, seat);

    return TRUE;
}

/**
 * @brief display_manager_start
 */
void display_manager_start(DisplayManager *manager)
{
    g_return_if_fail (manager != NULL);

    /* Disable Plymouth if no X servers are replacing it */
    if (plymouth_get_is_active ())
    {
        g_debug ("Stopping Plymouth, no displays replace it");
        plymouth_quit (FALSE);
    }
}

/**
 * @brief display_manager_stop
 */
void display_manager_stop(DisplayManager *manager)
{
    DisplayManagerPrivate *priv = display_manager_get_instance_private (manager);

    g_return_if_fail (manager != NULL);

    if (priv->stopping)
        return;

    g_debug ("Stopping display manager");

    priv->stopping = TRUE;

    /* Stop all the seats. Copy the list as it might be modified if a seat stops during this loop */
    GList *seats = g_list_copy (priv->seats);
    for (GList *link = seats; link; link = link->next)
    {
        Seat *seat = link->data;
        seat_stop (seat);
    }
    g_list_free (seats);

    check_stopped (manager);
}

/**
 * @brief display_manager_init
 * 初始化display manager，没被调用
 */
static void display_manager_init(DisplayManager *manager)
{
    /* Load the seat modules */
    seat_register_module ("local", SEAT_LOCAL_TYPE);
    seat_register_module ("xremote", SEAT_XREMOTE_TYPE);
}

/**
 * @brief display_manager_finalize
 * 不重要
 */
static void display_manager_finalize(GObject *object)
{
    DisplayManager *self = DISPLAY_MANAGER (object);
    DisplayManagerPrivate *priv = display_manager_get_instance_private (self);

    for (GList *link = priv->seats; link; link = link->next)
    {
        Seat *seat = link->data;
        g_signal_handlers_disconnect_matched (seat, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    }
    g_list_free_full (priv->seats, g_object_unref);

    G_OBJECT_CLASS (display_manager_parent_class)->finalize (object);
}

/**
 * @brief display_manager_class_init
 * 初始化display manager class，没被调用
 */
static void display_manager_class_init(DisplayManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = display_manager_finalize;

    signals[SEAT_ADDED] =
        g_signal_new (DISPLAY_MANAGER_SIGNAL_SEAT_ADDED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, seat_added),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SEAT_TYPE);
    signals[SEAT_REMOVED] =
        g_signal_new (DISPLAY_MANAGER_SIGNAL_SEAT_REMOVED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, seat_removed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, SEAT_TYPE);
    signals[STOPPED] =
        g_signal_new (DISPLAY_MANAGER_SIGNAL_STOPPED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, stopped),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}
