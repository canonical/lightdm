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
    SESSION_ADDED,
    SESSION_REMOVED,
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

    /* TRUE if display server can be shared for sessions */
    gboolean share_display_server;

    /* Name of guest account */
    gchar *guest_username;

    /* The display servers on this seat */
    GList *display_servers;

    /* The sessions on this seat */
    GList *sessions;

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

void
seat_set_share_display_server (Seat *seat, gboolean share_display_server)
{
    g_return_if_fail (seat != NULL);

    seat->priv->share_display_server = share_display_server;
}

gboolean
seat_start (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);
  
    SEAT_GET_CLASS (seat)->setup (seat);
    return SEAT_GET_CLASS (seat)->start (seat);
}

GList *
seat_get_sessions (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return seat->priv->sessions;
}

void
seat_set_active_session (Seat *seat, Session *session)
{
    g_return_if_fail (seat != NULL);
    SEAT_GET_CLASS (seat)->set_active_session (seat, session);
}

Session *
seat_get_active_session (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, NULL);
    return SEAT_GET_CLASS (seat)->get_active_session (seat);
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

gboolean
seat_get_greeter_allow_guest (Seat *seat)
{
    g_return_val_if_fail (seat != NULL, FALSE);  
    return seat_get_allow_guest (seat) && seat_get_boolean_property (seat, "greeter-allow-guest");
}

static gboolean
switch_to_user (Seat *seat, const gchar *username, gboolean unlock)
{
    GList *link;

    /* Switch to active display if it exists */
    // FIXME

    return FALSE;
}

static gboolean
run_script (Seat *seat, DisplayServer *display_server, const gchar *script_name, User *user)
{
    Process *script;
    gboolean result = FALSE;
  
    script = process_new ();

    process_set_command (script, script_name);

    /* Set POSIX variables */
    process_set_clear_environment (script, TRUE);
    process_set_env (script, "SHELL", "/bin/sh");

    /* Variables required for regression tests */
    if (g_getenv ("LIGHTDM_TEST_ROOT"))
    {
        process_set_env (script, "LIGHTDM_TEST_ROOT", g_getenv ("LIGHTDM_TEST_ROOT"));
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

    SEAT_GET_CLASS (seat)->run_script (seat, display_server, script);

    if (process_start (script, TRUE))
    {
        int exit_status;

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
seat_real_run_script (Seat *seat, DisplayServer *display_server, Process *process)
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

static void
session_stopped_cb (Session *session, Seat *seat)
{
    GList *link;
    const gchar *script;
  
    /* Cleanup */
    script = seat_get_string_property (seat, "session-cleanup-script");
    if (script)
        run_script (seat, session_get_display_server (session), script, session_get_user (session));

    if (seat->priv->guest_username && strcmp (session_get_username (session), seat->priv->guest_username) == 0)
    {
        g_free (seat->priv->guest_username);
        seat->priv->guest_username = NULL;
    }
}

static void
check_stopped (Seat *seat)
{
    if (seat->priv->stopping &&
        !seat->priv->stopped &&
        g_list_length (seat->priv->display_servers) == 0 &&
        g_list_length (seat->priv->sessions) == 0)
    {
        seat->priv->stopped = TRUE;
        g_debug ("Seat stopped");
        g_signal_emit (seat, signals[STOPPED], 0);
    }
}

static DisplayServer *
create_display_server (Seat *seat)
{
    return SEAT_GET_CLASS (seat)->create_display_server (seat);
}

gboolean
seat_switch_to_greeter (Seat *seat)
{
    DisplayServer *display_server;

    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    /* Switch to greeter if one open (shouldn't be though) */
    if (switch_to_user (seat, NULL, FALSE))
        return TRUE;

    // FIXME
}

gboolean
seat_switch_to_user (Seat *seat, const gchar *username, const gchar *session_name)
{
    g_return_val_if_fail (seat != NULL, FALSE);
    g_return_val_if_fail (username != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Switching to user %s", username);

    /* Switch to session if one open */
    if (switch_to_user (seat, username, FALSE))
        return TRUE;

    // FIXME

    return FALSE;
}

gboolean
seat_switch_to_guest (Seat *seat, const gchar *session_name)
{
    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch || !seat_get_allow_guest (seat))
        return FALSE;

    /* Switch to session if one open */
    if (seat->priv->guest_username)
    {
        g_debug ("Switching to existing guest account %s", seat->priv->guest_username);
        return switch_to_user (seat, seat->priv->guest_username, FALSE);
    }

    // FIXME

    return FALSE;
}

gboolean
seat_lock (Seat *seat, const gchar *username)
{
    g_return_val_if_fail (seat != NULL, FALSE);

    if (!seat->priv->can_switch)
        return FALSE;

    g_debug ("Locking seat");

    /* Switch to greeter if one open (shouldn't be though) */
    if (switch_to_user (seat, NULL, FALSE))
        return TRUE;

    // FIXME

    return FALSE;
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
    int autologin_timeout;
    gboolean autologin_guest;
    gboolean do_autologin;
    gboolean autologin_in_background;

    g_debug ("Starting seat");

    // FIXME
}

static void
seat_real_set_active_session (Seat *seat, Session *session)
{
}

static Session *
seat_real_get_active_session (Seat *seat)
{
    return NULL;
}

static void
seat_real_stop (Seat *seat)
{
    GList *list, *link;

    check_stopped (seat);
    if (seat->priv->stopped)
        return;

    /* Stop all the display servers and sessions on the seat. Copy the list as
     * it might be modified if a display server / session stops during this loop */
    list = g_list_copy (seat->priv->display_servers);
    for (link = list; link; link = link->next)
    {
        DisplayServer *display_server = link->data;
        display_server_stop (display_server);
    }
    g_list_free (list);
    list = g_list_copy (seat->priv->sessions);
    for (link = list; link; link = link->next)
    {
        Session *session = link->data;
        session_stop (session);
    }
    g_list_free (list);
}

static void
seat_init (Seat *seat)
{
    seat->priv = G_TYPE_INSTANCE_GET_PRIVATE (seat, SEAT_TYPE, SeatPrivate);
    seat->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    seat->priv->share_display_server = TRUE;
}

static void
seat_finalize (GObject *object)
{
    Seat *self;
    GList *link;

    self = SEAT (object);

    g_hash_table_unref (self->priv->properties);
    g_free (self->priv->guest_username);
    for (link = self->priv->display_servers; link; link = link->next)
    {
        DisplayServer *display_server = link->data;
        g_signal_handlers_disconnect_matched (display_server, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    }  
    g_list_free_full (self->priv->display_servers, g_object_unref);
    for (link = self->priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        g_signal_handlers_disconnect_matched (session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
    }  
    g_list_free_full (self->priv->sessions, g_object_unref);

    G_OBJECT_CLASS (seat_parent_class)->finalize (object);
}

static void
seat_class_init (SeatClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->setup = seat_real_setup;
    klass->start = seat_real_start;
    klass->set_active_session = seat_real_set_active_session;
    klass->get_active_session = seat_real_get_active_session;
    klass->run_script = seat_real_run_script;
    klass->stop = seat_real_stop;

    object_class->finalize = seat_finalize;

    g_type_class_add_private (klass, sizeof (SeatPrivate));

    signals[SESSION_ADDED] =
        g_signal_new ("session-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, session_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, SESSION_TYPE);
    signals[SESSION_REMOVED] =
        g_signal_new ("session-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, session_removed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, SESSION_TYPE);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (SeatClass, stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
