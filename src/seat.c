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
    /* Configuration for this seat */
    gchar *config_section;

    /* TRUE if able to switch users */
    gboolean can_switch;

    /* TRUE if allowed to log into guest account */
    gboolean allow_guest;

    /* Name of guest account */
    gchar *guest_username;

    /* User to automatically log in as */
    gchar *autologin_username;
    gboolean autologin_guest;
    guint autologin_timeout;

    /* The displays for this seat */
    GList *displays;

    /* The active display */
    Display *active_display;

    /* TRUE if stopping this seat (waiting for displays to stop) */
    gboolean stopping;
};

G_DEFINE_TYPE (Seat, seat, G_TYPE_OBJECT);

typedef struct
{
    const gchar *name;
    GType type;
} SeatModule;
static GHashTable *seat_modules = NULL;

void
seat_register_module (const gchar *name, GType type)
{
    SeatModule *module;

    if (!seat_modules)
        seat_modules = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    g_debug ("Registered seat module %s", name);

    module = g_malloc0 (sizeof (SeatModule));
    module->name = g_strdup (name);
    module->type = type;
    g_hash_table_insert (seat_modules, g_strdup (name), module);
}

Seat *
seat_new (const gchar *module, const gchar *config_section)
{
    Seat *seat;
    SeatModule *m = NULL;

    if (seat_modules)
        m = g_hash_table_lookup (seat_modules, module);
    if (!m)
        return NULL;

    seat = g_object_new (m->type, NULL);
    seat->priv->config_section = g_strdup (config_section);

    return seat;
}

const gchar *
seat_get_config_section (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return seat->priv->config_section;
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
  
    SEAT_GET_CLASS (seat)->setup (seat);
    return SEAT_GET_CLASS (seat)->start (seat);
}

GList *
seat_get_displays (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return seat->priv->displays;
}

void
seat_set_active_display (Seat *seat, Display *display)
{
    g_return_if_fail (seat != NULL);
    SEAT_GET_CLASS (seat)->set_active_display (seat, display);
}

Display *
seat_get_active_display (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return seat->priv->active_display;
}

gboolean
seat_get_can_switch (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return seat->priv->can_switch;
}

gboolean
seat_get_allow_guest (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);  
    return seat->priv->allow_guest && guest_account_is_installed ();
}

static gboolean
switch_to_user (Seat *seat, const gchar *username)
{
    GList *link;

    /* Switch to active display if it exists */
    for (link = seat->priv->displays; link; link = link->next)
    {
        Display *display = link->data;

        /* If already logged in, then switch to that display */
        if (g_strcmp0 (display_get_username (display), username) == 0)
        {
            // FIXME: Use display_get_name
            g_debug ("Switching to user %s session on display %s", username, xserver_get_address (XSERVER (display_get_display_server (display))));
            seat_set_active_display (seat, display);
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
display_switch_to_user_cb (Display *display, const gchar *username, Seat *seat)
{
    return switch_to_user (seat, username);
}

static gboolean
display_switch_to_guest_cb (Display *display, Seat *seat)
{
    /* No guest account */
    if (!seat->priv->guest_username)
        return FALSE;

    return display_switch_to_user_cb (display, seat->priv->guest_username, seat);
}

static const gchar *
display_get_guest_username_cb (Display *display, Seat *seat)
{
    if (seat->priv->guest_username)
        return seat->priv->guest_username;

    seat->priv->guest_username = guest_account_setup ();
    return g_strdup (seat->priv->guest_username);
}

static void
display_session_started_cb (Display *display, Seat *seat)
{
    /* Switch to this new display */
    SEAT_GET_CLASS (seat)->set_active_display (seat, display);
}

static void
display_session_stopped_cb (Display *display, Seat *seat)
{
    Session *session;

    session = display_get_session (display);
    if (seat->priv->guest_username && strcmp (user_get_name (session_get_user (session)), seat->priv->guest_username) == 0)
    {
        guest_account_cleanup (seat->priv->guest_username);
        g_free (seat->priv->guest_username);
        seat->priv->guest_username = NULL;
    }
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

static gboolean
switch_to_user_or_start_greeter (Seat *seat, const gchar *username, gboolean is_guest, const gchar *session_name, gboolean autologin)
{
    GList *link;
    Display *new_display = NULL;

    /* Switch to active display if it exists */
    for (link = seat->priv->displays; link; link = link->next)
    {
        Display *display = link->data;

        /* If already logged in, then switch to that display and stop the greeter display */
        if (g_strcmp0 (display_get_username (display), username) == 0)
        {
            // FIXME: Use display_get_name
            if (username)
                g_debug ("Switching to user %s session on display %s", username, xserver_get_address (XSERVER (display_get_display_server (display))));
            else
                g_debug ("Switching to greeter on display %s", xserver_get_address (XSERVER (display_get_display_server (display))));
            seat_set_active_display (seat, display);
            return TRUE;
        }
    }

    /* They don't exist, so start a greeter */
    if (is_guest)
        g_debug ("Starting new greeter to authenticate guest");        
    else if (username)
        g_debug ("Starting new greeter to authenticate user %s", username);
    else
        g_debug ("Starting new greeter");

    new_display = SEAT_GET_CLASS (seat)->add_display (seat);
    display_load_config (DISPLAY (new_display), seat->priv->config_section);
    g_signal_connect (new_display, "switch-to-user", G_CALLBACK (display_switch_to_user_cb), seat);
    g_signal_connect (new_display, "switch-to-guest", G_CALLBACK (display_switch_to_guest_cb), seat);
    g_signal_connect (new_display, "get-guest-username", G_CALLBACK (display_get_guest_username_cb), seat);
    g_signal_connect (new_display, "session-started", G_CALLBACK (display_session_started_cb), seat);
    g_signal_connect (new_display, "session-stopped", G_CALLBACK (display_session_stopped_cb), seat);
    g_signal_connect (new_display, "stopped", G_CALLBACK (display_stopped_cb), seat);
    display_set_allow_guest (new_display, seat_get_allow_guest (seat));
    if (is_guest)
        display_set_default_user (new_display, NULL, TRUE, autologin, 0);
    else if (username)
        display_set_default_user (new_display, username, FALSE, autologin, 0);

    seat->priv->displays = g_list_append (seat->priv->displays, new_display);
    g_signal_emit (seat, signals[DISPLAY_ADDED], 0, new_display);

    /* Switch to this new display */
    if (!seat->priv->active_display)
        seat_set_active_display (seat, new_display);

    return display_start (new_display);
}

gboolean
seat_switch_to_greeter (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Switching to greeter");
    return switch_to_user_or_start_greeter (seat, NULL, FALSE, NULL, FALSE);
}

gboolean
seat_switch_to_user (Seat *seat, const gchar *username, const gchar *session_name)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    g_return_val_if_fail (username != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Switching to user %s", username);
    return switch_to_user_or_start_greeter (seat, username, FALSE, session_name, FALSE);
}

gboolean
seat_switch_to_guest (Seat *seat, const gchar *session_name)
{
    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch || !seat_get_allow_guest (seat))
        return FALSE;

    if (seat->priv->guest_username)
        g_debug ("Switching to existing guest account %s", seat->priv->guest_username);
    else
        g_debug ("Switching to new guest account");
    return switch_to_user_or_start_greeter (seat, seat->priv->guest_username, TRUE, session_name, TRUE);
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

static void
seat_real_setup (Seat *seat)
{
    if (seat->priv->config_section && config_has_key (config_get_instance (), seat->priv->config_section, "allow-guest"))
        seat->priv->allow_guest = config_get_boolean (config_get_instance (), seat->priv->config_section, "allow-guest");
    else if (config_has_key (config_get_instance (), "SeatDefaults", "allow-guest"))
        seat->priv->allow_guest = config_get_boolean (config_get_instance (), "SeatDefaults", "allow-guest");
    if (seat->priv->config_section && config_has_key (config_get_instance (), seat->priv->config_section, "autologin-guest"))
        seat->priv->autologin_guest = config_get_boolean (config_get_instance (), seat->priv->config_section, "autologin-guest");
    else if (config_has_key (config_get_instance (), "SeatDefaults", "autologin-guest"))
        seat->priv->autologin_guest = config_get_boolean (config_get_instance (), "SeatDefaults", "autologin-guest");
    if (seat->priv->config_section)
        seat->priv->autologin_username = config_get_string (config_get_instance (), seat->priv->config_section, "autologin-user");
    if (!seat->priv->autologin_username)
        seat->priv->autologin_username = config_get_string (config_get_instance (), "SeatDefaults", "autologin-user");
    if (seat->priv->config_section && config_has_key (config_get_instance (), seat->priv->config_section, "autologin-user-timeout"))
        seat->priv->autologin_timeout = config_get_integer (config_get_instance (), seat->priv->config_section, "autologin-user-timeout");
    else
        seat->priv->autologin_timeout = config_get_integer (config_get_instance (), "SeatDefaults", "autologin-user-timeout");
    if (seat->priv->autologin_timeout < 0)
        seat->priv->autologin_timeout = 0;
}

static gboolean
seat_real_start (Seat *seat)
{
    g_debug ("Starting seat");

    /* Start showing a greeter */
    if (seat->priv->autologin_username)
        return switch_to_user_or_start_greeter (seat, seat->priv->autologin_username, FALSE, NULL, TRUE);
    else if (seat->priv->autologin_guest)
        return switch_to_user_or_start_greeter (seat, NULL, TRUE, NULL, TRUE);
    else
        return switch_to_user_or_start_greeter (seat, NULL, FALSE, NULL, FALSE);
}

static Display *
seat_real_add_display (Seat *seat)
{
    return NULL;
}

static void
seat_real_set_active_display (Seat *seat, Display *display)
{
    if (display == seat->priv->active_display)
        return;

    if (seat->priv->active_display)
    {
        /* Stop the existing display if it is a greeter */
        if (!display_get_username (seat->priv->active_display))
        {
            g_debug ("Stopping greeter display being switched from");
            display_stop (seat->priv->active_display);
        }
        g_object_unref (seat->priv->active_display);
    }
    seat->priv->active_display = g_object_ref (display);
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

    g_free (self->priv->config_section);
    g_list_free_full (self->priv->displays, g_object_unref);
    if (self->priv->active_display)
        g_object_unref (self->priv->active_display);

    G_OBJECT_CLASS (seat_parent_class)->finalize (object);
}

static void
seat_class_init (SeatClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->setup = seat_real_setup;
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
