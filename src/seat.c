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

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "seat.h"
#include "guest-account.h"

enum {
    DISPLAY_ADDED,
    DISPLAY_REMOVED,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct SeatPrivate
{
    /* Configuration for this seat */
    GHashTable *properties;

    /* TRUE if able to switch users */
    gboolean can_switch;

    /* Name of guest account */
    gchar *guest_username;

    /* The displays for this seat */
    GList *displays;

    /* The active display */
    Display *active_display;

    /* TRUE if stopping this seat (waiting for displays to stop) */
    gboolean stopping;

    /* TRUE if stopped */
    gboolean stopped;
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
seat_new (const gchar *module_name)
{
    Seat *seat;
    SeatModule *m = NULL;
  
    g_return_val_if_fail (module_name != NULL, NULL);

    if (seat_modules)
        m = g_hash_table_lookup (seat_modules, module_name);
    if (!m)
        return NULL;

    seat = g_object_new (m->type, NULL);

    return seat;
}

void
seat_set_property (Seat *seat, const gchar *name, const gchar *value)
{
    g_return_if_fail (seat != NULL);
    g_hash_table_insert (seat->priv->properties, g_strdup (name), g_strdup (value));
}

gboolean
seat_has_property (Seat *seat, const gchar *name)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return g_hash_table_lookup (seat->priv->properties, name) != NULL;
}

const gchar *
seat_get_string_property (Seat *seat, const gchar *name)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return g_hash_table_lookup (seat->priv->properties, name);
}

gboolean
seat_get_boolean_property (Seat *seat, const gchar *name)
{
    return g_strcmp0 (seat_get_string_property (seat, name), "true") == 0;
}

gint
seat_get_integer_property (Seat *seat, const gchar *name)
{
    const gchar *value;

    value = seat_get_string_property (seat, name);
    return value ? atoi (value) : 0;
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
    return seat_get_boolean_property (seat, "allow-guest") && guest_account_is_installed ();
}

static gboolean
switch_to_user (Seat *seat, const gchar *username, gboolean unlock)
{
    GList *link;

    /* Switch to active display if it exists */
    for (link = seat->priv->displays; link; link = link->next)
    {
        Display *display = link->data;

        /* If already logged in, then switch to that display */
        if (g_strcmp0 (display_get_username (display), username) == 0)        
        {
            if (username)
                g_debug ("Switching to existing session for user %s", username);
            else
                g_debug ("Switching to existing greeter");
            if (unlock)
                display_unlock (display);
            seat_set_active_display (seat, display);
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
display_switch_to_user_cb (Display *display, User *user, Seat *seat)
{
    return switch_to_user (seat, user_get_name (user), TRUE);
}

static gboolean
display_switch_to_guest_cb (Display *display, Seat *seat)
{
    /* No guest account */
    if (!seat->priv->guest_username)
        return FALSE;

    return switch_to_user (seat, seat->priv->guest_username, TRUE);
}

static const gchar *
display_get_guest_username_cb (Display *display, Seat *seat)
{
    if (seat->priv->guest_username)
        return seat->priv->guest_username;

    seat->priv->guest_username = guest_account_setup ();
    return g_strdup (seat->priv->guest_username);
}

static gboolean
run_script (Seat *seat, Display *display, const gchar *script_name, User *user)
{
    Process *script;
    gboolean result = FALSE;
  
    script = process_new ();

    process_set_command (script, script_name);

    /* Set POSIX variables */
    process_set_clear_environment (script, TRUE);
    process_set_env (script, "SHELL", "/bin/sh");

    /* Variables required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"))
    {
        process_set_env (script, "LIGHTDM_TEST_STATUS_SOCKET", g_getenv ("LIGHTDM_TEST_STATUS_SOCKET"));
        process_set_env (script, "LIGHTDM_TEST_CONFIG", g_getenv ("LIGHTDM_TEST_CONFIG"));
        process_set_env (script, "LIGHTDM_TEST_HOME_DIR", g_getenv ("LIGHTDM_TEST_HOME_DIR"));
        process_set_env (script, "LD_PRELOAD", g_getenv ("LD_PRELOAD"));
        process_set_env (script, "LD_LIBRARY_PATH", g_getenv ("LD_LIBRARY_PATH"));
        process_set_env (script, "PATH", g_getenv ("PATH"));
    }
    else
        process_set_env (script, "PATH", "/usr/local/bin:/usr/bin:/bin");

    if (user)
    {
        process_set_env (script, "USER", user_get_name (user));
        process_set_env (script, "LOGNAME", user_get_name (user));
        process_set_env (script, "HOME", user_get_home_directory (user));
    }
    else
        process_set_env (script, "HOME", "/");

    SEAT_GET_CLASS (seat)->run_script (seat, display, script);

    if (process_start (script))
    {
        int exit_status;

        process_wait (script);

        exit_status = process_get_exit_status (script);
        if (WIFEXITED (exit_status))
        {
            g_debug ("Exit status of %s: %d", script_name, WEXITSTATUS (exit_status));
            result = WEXITSTATUS (exit_status) == EXIT_SUCCESS;
        }
    }

    g_object_unref (script);

    return result;
}

static void
seat_real_run_script (Seat *seat, Display *display, Process *process)
{  
}

static void
emit_upstart_signal (const gchar *signal)
{
    g_return_if_fail (signal != NULL);
    g_return_if_fail (signal[0] != 0);

    if (getuid () != 0)
        return;

    gchar *cmd = g_strdup_printf ("initctl -q emit %s DISPLAY_MANAGER=lightdm", signal);
    g_spawn_command_line_async (cmd, NULL); /* OK if it fails, probably not installed */
    g_free (cmd);
}

static gboolean
display_display_server_ready_cb (Display *display, Seat *seat)
{
    const gchar *script;

    /* Run setup script */
    script = seat_get_string_property (seat, "display-setup-script");
    if (script && !run_script (seat, display, script, NULL))
        return FALSE;

    emit_upstart_signal ("login-session-start");

    return TRUE;
}

static Session *
display_create_session_cb (Display *display, Seat *seat)
{
    return SEAT_GET_CLASS (seat)->create_session (seat, display);
}

static gboolean
display_start_greeter_cb (Display *display, Seat *seat)
{
    Session *session;
    const gchar *script;

    session = display_get_session (display);

    script = seat_get_string_property (seat, "greeter-setup-script");
    if (script)
        return !run_script (seat, display, script, session_get_user (session));
    else
        return FALSE;
}

static gboolean
display_start_session_cb (Display *display, Seat *seat)
{
    Session *session;
    const gchar *script;

    session = display_get_session (display);

    script = seat_get_string_property (seat, "session-setup-script");
    if (script)
        return !run_script (seat, display, script, session_get_user (session));
    else
        return FALSE;
}

static void
session_stopped_cb (Session *session, Seat *seat)
{
    Display *display = NULL;
    GList *link;
    const gchar *script;
  
    /* Work out what display this session is on, it's a bit hacky because we really should know already... */
    for (link = seat->priv->displays; link; link = link->next)
    {
        Display *d = link->data;
        if (display_get_session (d) == session)
        {
            display = d;
            break;
        }
    }
    g_return_if_fail (display != NULL);

    /* Cleanup */
    script = seat_get_string_property (seat, "session-cleanup-script");
    if (script)
        run_script (seat, display, script, session_get_user (session));

    if (seat->priv->guest_username && strcmp (user_get_name (session_get_user (session)), seat->priv->guest_username) == 0)
    {
        guest_account_cleanup (seat->priv->guest_username);
        g_free (seat->priv->guest_username);
        seat->priv->guest_username = NULL;
    }
}

static gboolean
display_session_started_cb (Display *display, Seat *seat)
{
    g_signal_connect (display_get_session (display), "stopped", G_CALLBACK (session_stopped_cb), seat);
    emit_upstart_signal ("desktop-session-start");
    return FALSE;
}

static void
display_ready_cb (Display *display, Seat *seat)
{
    /* Switch to this new display */
    g_debug ("New display ready, switching to it");
    SEAT_GET_CLASS (seat)->set_active_display (seat, display);
}

static void
check_stopped (Seat *seat)
{
    if (seat->priv->stopping &&
        !seat->priv->stopped &&
        g_list_length (seat->priv->displays) == 0)
    {
        seat->priv->stopped = TRUE;
        g_debug ("Seat stopped");
        g_signal_emit (seat, signals[STOPPED], 0);
    }
}

static void
display_stopped_cb (Display *display, Seat *seat)
{
    seat->priv->displays = g_list_remove (seat->priv->displays, display);
    g_signal_handlers_disconnect_matched (display, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, seat);
    g_signal_emit (seat, signals[DISPLAY_REMOVED], 0, display);
    g_object_unref (display);

    check_stopped (seat);
}

static gboolean
switch_to_user_or_start_greeter (Seat *seat, const gchar *username, gboolean is_guest, const gchar *session_name, gboolean is_lock, gboolean autologin)
{
    Display *display;
    DisplayServer *display_server;

    /* Switch to existing if it exists */
    if (switch_to_user (seat, username, FALSE))
        return TRUE;

    /* If one don't exist then start a greeter */
    if (autologin)
    {
        if (is_guest)
            g_debug ("Starting new display for automatic guest login");
        else if (username)
            g_debug ("Starting new display for automatic login as user %s", username);
        else
            g_debug ("Starting new display for greeter");
    }
    else
    {
        if (is_guest)
            g_debug ("Starting new display for greeter with guest selected");
        else if (username)
            g_debug ("Starting new display for greeter with user %s selected", username);
        else if (is_lock)
            g_debug ("Starting new display for greeter (lock screen)");
        else
            g_debug ("Starting new display for greeter");
    }

    display_server = SEAT_GET_CLASS (seat)->create_display_server (seat);
    display = display_new (display_server);
    g_object_unref (display_server);

    g_signal_connect (display, "display-server-ready", G_CALLBACK (display_display_server_ready_cb), seat);  
    g_signal_connect (display, "switch-to-user", G_CALLBACK (display_switch_to_user_cb), seat);
    g_signal_connect (display, "switch-to-guest", G_CALLBACK (display_switch_to_guest_cb), seat);
    g_signal_connect (display, "get-guest-username", G_CALLBACK (display_get_guest_username_cb), seat);
    g_signal_connect (display, "create-session", G_CALLBACK (display_create_session_cb), seat);
    g_signal_connect (display, "start-greeter", G_CALLBACK (display_start_greeter_cb), seat);
    g_signal_connect (display, "start-session", G_CALLBACK (display_start_session_cb), seat);
    g_signal_connect_after (display, "start-session", G_CALLBACK (display_session_started_cb), seat);
    g_signal_connect (display, "ready", G_CALLBACK (display_ready_cb), seat);
    g_signal_connect (display, "stopped", G_CALLBACK (display_stopped_cb), seat);
    display_set_greeter_session (display, seat_get_string_property (seat, "greeter-session"));
    display_set_session_wrapper (display, seat_get_string_property (seat, "session-wrapper"));
    display_set_hide_users_hint (display, seat_get_boolean_property (seat, "greeter-hide-users"));
    if (is_lock)
        display_set_lock_hint (display, TRUE);
    display_set_allow_guest (display, seat_get_allow_guest (seat));
    if (autologin)
        display_set_autologin_user (display, username, is_guest, 0);
    else
        display_set_select_user_hint (display, username, is_guest);
    if (!session_name)
        session_name = seat_get_string_property (seat, "user-session");
    display_set_user_session (display, session_name);

    seat->priv->displays = g_list_append (seat->priv->displays, display);
    g_signal_emit (seat, signals[DISPLAY_ADDED], 0, display);

    /* Switch to this display if currently not looking at anything */
    if (!seat->priv->active_display)
        seat_set_active_display (seat, display);

    return display_start (display);
}

gboolean
seat_switch_to_greeter (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Switching to greeter");
    return switch_to_user_or_start_greeter (seat, NULL, FALSE, NULL, FALSE, FALSE);
}

gboolean
seat_switch_to_user (Seat *seat, const gchar *username, const gchar *session_name)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    g_return_val_if_fail (username != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Switching to user %s", username);
    return switch_to_user_or_start_greeter (seat, username, FALSE, session_name, FALSE, FALSE);
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
    return switch_to_user_or_start_greeter (seat, seat->priv->guest_username, TRUE, session_name, FALSE, TRUE);
}

gboolean
seat_lock (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Locking seat");
    return switch_to_user_or_start_greeter (seat, NULL, FALSE, NULL, TRUE, FALSE);
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

gboolean
seat_get_is_stopping (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    return seat->priv->stopping;
}

static void
seat_real_setup (Seat *seat)
{
}

static gboolean
seat_real_start (Seat *seat)
{
    const gchar *autologin_username;

    g_debug ("Starting seat");

    /* Start showing a greeter */
    autologin_username = seat_get_string_property (seat, "autologin-user");
    if (g_strcmp0 (autologin_username, "") == 0)
        autologin_username = NULL;

    if (autologin_username)
        return switch_to_user_or_start_greeter (seat, autologin_username, FALSE, NULL, FALSE, TRUE);
    else if (seat_get_boolean_property (seat, "autologin-guest"))
        return switch_to_user_or_start_greeter (seat, NULL, TRUE, NULL, FALSE, TRUE);
    else
        return switch_to_user_or_start_greeter (seat, NULL, FALSE, NULL, FALSE, FALSE);
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

    check_stopped (seat);
    if (seat->priv->stopped)
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
    seat->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
seat_finalize (GObject *object)
{
    Seat *self;

    self = SEAT (object);

    g_hash_table_unref (self->priv->properties);
    g_free (self->priv->guest_username);
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
    klass->set_active_display = seat_real_set_active_display;
    klass->run_script = seat_real_run_script;
    klass->stop = seat_real_stop;

    object_class->finalize = seat_finalize;

    g_type_class_add_private (klass, sizeof (SeatPrivate));

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
