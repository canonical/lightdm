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
#include "guest-account.h"

enum {
    STARTED,
    DISPLAY_ADDED,
    DISPLAY_REMOVED,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct SeatPrivate
{
    /* True if able to switch users */
    gboolean can_switch;

    /* User to automatically log in as */
    gchar *autologin_username;
    gboolean autologin_guest;
    guint autologin_timeout;

    /* The displays for this seat */
    GList *displays;

    /* TRUE if stopping this seat (waiting for displays to stop) */
    gboolean stopping;
};

G_DEFINE_TYPE (Seat, seat, G_TYPE_OBJECT);

void
seat_load_config (Seat *seat, const gchar *config_section)
{
    if (config_section && config_has_key (config_get_instance (), config_section, "autologin-guest"))
        seat->priv->autologin_guest = config_get_boolean (config_get_instance (), config_section, "autologin-guest");
    else if (config_has_key (config_get_instance (), "SeatDefaults", "autologin-guest"))
        seat->priv->autologin_guest = config_get_boolean (config_get_instance (), "SeatDefaults", "autologin-guest");
    if (config_section)
        seat->priv->autologin_username = config_get_string (config_get_instance (), config_section, "autologin-user");
    if (!seat->priv->autologin_username)
        seat->priv->autologin_username = config_get_string (config_get_instance (), "SeatDefaults", "autologin-user");
    if (config_section && config_has_key (config_get_instance (), config_section, "autologin-user-timeout"))
        seat->priv->autologin_timeout = config_get_integer (config_get_instance (), config_section, "autologin-user-timeout");
    else
        seat->priv->autologin_timeout = config_get_integer (config_get_instance (), "SeatDefaults", "autologin-user-timeout");
    if (seat->priv->autologin_timeout < 0)
        seat->priv->autologin_timeout = 0;
}

void
seat_set_can_switch (Seat *seat, gboolean can_switch)
{
    g_return_if_fail (seat != NULL);

    seat->priv->can_switch = can_switch;
}

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

gboolean
seat_get_can_switch (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return seat->priv->can_switch;
}

static gboolean
display_activate_user_cb (Display *display, const gchar *username, Seat *seat)
{
    GList *link;

    /* Switch to active display if it exists */
    for (link = seat->priv->displays; link; link = link->next)
    {
        Display *d = link->data;
        Session *session;

        if (d == display)
            continue;

        session = display_get_session (d);
        if (!session || session_get_is_greeter (session))
            continue;
 
        /* If already logged in, then switch to that display and stop the greeter display */
        if (g_strcmp0 (user_get_name (session_get_user (session)), username) == 0)
        {
            // FIXME: Use display_get_name
            g_debug ("Switching to user %s session on display %s", username, xserver_get_address (XSERVER (display_get_display_server (display))));
            SEAT_GET_CLASS (seat)->set_active_display (seat, display);
            return TRUE;
        }
    }

    /* Otherwise start on this display */
    return FALSE;
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
    g_signal_handlers_disconnect_matched (display, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    g_signal_emit (seat, signals[DISPLAY_REMOVED], 0, display);
    g_object_unref (display);

    if (seat->priv->stopping)
        check_stopped (seat);
}

static Display *
add_display (Seat *seat)
{
    Display *display;

    display = SEAT_GET_CLASS (seat)->add_display (seat);
    g_signal_connect (display, "activate-user", G_CALLBACK (display_activate_user_cb), seat);
    g_signal_connect (display, "stopped", G_CALLBACK (display_stopped_cb), seat);
    seat->priv->displays = g_list_append (seat->priv->displays, g_object_ref (display));
    g_signal_emit (seat, signals[DISPLAY_ADDED], 0, display);
  
    return display;
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
        Session *session;

        session = display_get_session (display);
        if (!session)
            continue;
 
        /* Shouldn't be any other greeters running, close them if so */
        if (!session || session_get_is_greeter (session))
        {
            display_stop (display);
            continue;
        }

        /* If already logged in, then switch to that display */
        if (g_strcmp0 (user_get_name (session_get_user (session)), username) == 0)
        {
            // FIXME: Use display_get_name
            g_debug ("Switching to user %s session on display %s", username, xserver_get_address (XSERVER (display_get_display_server (display))));
            SEAT_GET_CLASS (seat)->set_active_display (seat, display);
            return TRUE;
        }
    }

    display = add_display (seat);
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

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Showing greeter");
  
    return switch_to_user (seat, NULL, FALSE);
}

gboolean
seat_switch_to_user (Seat *seat, const gchar *username)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    g_return_val_if_fail (username != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Switching to user %s", username);
    return switch_to_user (seat, username, FALSE);
}

gboolean
seat_switch_to_guest (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch)
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

    if (seat->priv->stopping)
        return;

    g_debug ("Stopping seat");
    seat->priv->stopping = TRUE;
    SEAT_GET_CLASS (seat)->stop (seat);
}

static gboolean
seat_real_start (Seat *seat)
{
    Display *display;

    display = add_display (seat);

    if (seat->priv->autologin_username)
        display_set_default_user (display, seat->priv->autologin_username, FALSE, FALSE, seat->priv->autologin_timeout);
    else if (seat->priv->autologin_guest)
        display_set_default_user (display, NULL, TRUE, FALSE, seat->priv->autologin_timeout);

    return display_start (display);
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

    self = SEAT (object);

    g_list_free_full (self->priv->displays, g_object_unref);

    G_OBJECT_CLASS (seat_parent_class)->finalize (object);
}

static void
seat_class_init (SeatClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->start = seat_real_start;
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
    signals[DISPLAY_ADDED] =
        g_signal_new ("display-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, display_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, DISPLAY_TYPE);
    signals[DISPLAY_REMOVED] =
        g_signal_new ("display-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, display_removed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, DISPLAY_TYPE);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
