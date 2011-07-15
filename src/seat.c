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

#include <string.h>

#include "seat.h"
#include "configuration.h"
#include "display.h"
#include "xserver.h"
#include "vt.h"
#include "guest-account.h"

enum {
    STARTED,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct SeatPrivate
{
    /* The displays for this seat */
    GList *displays;

    /* TRUE if stopping this seat (waiting for displays to stop) */
    gboolean stopping;
};

G_DEFINE_TYPE (Seat, seat, G_TYPE_OBJECT);

gboolean
seat_start (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return SEAT_GET_CLASS (seat)->start (seat);
}

GList *
seat_get_displays (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return seat->priv->displays;
}

void
seat_remove_display (Seat *seat, Display *display)
{
    g_return_if_fail (seat != NULL);
    g_return_if_fail (display != NULL);
    seat->priv->displays = g_list_remove (seat->priv->displays, display);
}

gboolean
seat_get_can_switch (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return SEAT_GET_CLASS (seat)->get_can_switch (seat);
}

static gboolean
display_activate_user_cb (Display *display, const gchar *username, Seat *seat)
{
    return seat_switch_to_user (seat, username);
}

static gboolean
check_stopped (Seat *seat)
{
    if (g_list_length (seat->priv->displays) == 0)
    {
        g_debug ("Seat stopped");
        g_signal_emit (seat, signals[STOPPED], 0);
        return TRUE;
    }
    return FALSE;
}

static void
display_stopped_cb (Display *display, Seat *seat)
{
    seat->priv->displays = g_list_remove (seat->priv->displays, display);
    if (seat->priv->stopping)
        check_stopped (seat);
}

static gboolean
switch_to_user (Seat *seat, const gchar *username, gboolean is_guest)
{
    Display *display;
    GList *link;

    /* Switch to active display if it exists */
    for (link = seat->priv->displays; link; link = link->next)
    {
        display = link->data;
 
        /* Shouldn't be any other greeters running, close them if so */
        if (display_get_greeter (display))
        {
            display_stop (display);
            continue;
        }

        /* If already logged in, then switch to that display */
        if (g_strcmp0 (display_get_session_user (display), username) == 0)
        {
            g_debug ("Switching to user %s session on display %s", username, xserver_get_address (display_get_xserver (display)));
            SEAT_GET_CLASS (seat)->set_active_display (seat, display);
            return TRUE;
        }
    }

    display = SEAT_GET_CLASS (seat)->add_display (seat);
    g_signal_connect (display, "activate-user", G_CALLBACK (display_activate_user_cb), seat);
    g_signal_connect (display, "stopped", G_CALLBACK (display_stopped_cb), seat);
    seat->priv->displays = g_list_append (seat->priv->displays, display);

    if (is_guest)
        display_set_default_user (display, NULL, TRUE, FALSE, 0);
    else if (username)
        display_set_default_user (display, username, FALSE, TRUE, 0);

    return display_start (display);
}

gboolean
seat_switch_to_greeter (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
  
    if (!seat_get_can_switch (seat))
        return FALSE;

    g_debug ("Showing greeter");
  
    return switch_to_user (seat, NULL, FALSE);
}

gboolean
seat_switch_to_user (Seat *seat, const gchar *username)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    g_return_val_if_fail (username != NULL, FALSE);

    if (!seat_get_can_switch (seat))
        return FALSE;

    g_debug ("Switching to user %s", username);
    return switch_to_user (seat, username, FALSE);
}

gboolean
seat_switch_to_guest (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat_get_can_switch (seat))
        return FALSE;

    if (!guest_account_get_is_enabled ())
        return FALSE;

    g_debug ("Switching to guest account");  
    return switch_to_user (seat, guest_account_get_username (), TRUE);
}

void
seat_stop (Seat *seat)
{
    g_return_if_fail (seat != NULL);

    g_debug ("Stopping seat");
    seat->priv->stopping = TRUE;
    SEAT_GET_CLASS (seat)->stop (seat);
}

static gboolean
seat_real_start (Seat *seat)
{
    return seat_switch_to_greeter (seat);
}

static gboolean
seat_real_get_can_switch (Seat *seat)
{
    return FALSE;
}

static Display *
seat_real_add_display (Seat *seat)
{
    return NULL;
}

static void
seat_real_set_active_display (Seat *seat, Display *display)
{
}

static void
seat_real_stop (Seat *seat)
{
    GList *link;

    if (check_stopped (seat))
        return;

    for (link = seat->priv->displays; link; link = link->next)
    {
        Display *display = link->data;
        display_stop (display);
    }
}

static void
seat_init (Seat *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_TYPE, SeatPrivate);
}

static void
seat_finalize (GObject *object)
{
    Seat *self;
    GList *link;

    self = SEAT (object);
  
    for (link = self->priv->displays; link; link = link->next)
        g_object_unref (link->data);
    g_list_free (self->priv->displays);

    G_OBJECT_CLASS (seat_parent_class)->finalize (object);
}

static void
seat_class_init (SeatClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->start = seat_real_start;
    klass->get_can_switch = seat_real_get_can_switch;
    klass->add_display = seat_real_add_display;
    klass->set_active_display = seat_real_set_active_display;
    klass->stop = seat_real_stop;

    object_class->finalize = seat_finalize;

    g_type_class_add_private (klass, sizeof (SeatPrivate));

    signals[STARTED] =
        g_signal_new ("started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, started),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
