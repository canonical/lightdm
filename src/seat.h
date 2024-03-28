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

#ifndef SEAT_H_
#define SEAT_H_

#include <glib-object.h>
#include "display-server.h"
#include "greeter-session.h"
#include "session.h"
#include "process.h"
#include "logger.h"

G_BEGIN_DECLS

#define SEAT_TYPE           (seat_get_type())
#define SEAT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_TYPE, Seat))
#define SEAT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), SEAT_TYPE, SeatClass))
#define SEAT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SEAT_TYPE, SeatClass))

#define SEAT_SIGNAL_SESSION_ADDED        "session-added"
#define SEAT_SIGNAL_RUNNING_USER_SESSION "running-user-session"
#define SEAT_SIGNAL_SESSION_REMOVED      "session-removed"
#define SEAT_SIGNAL_STOPPED              "stopped"

typedef struct
{
    GObject parent_instance;
} Seat;

typedef struct
{
    GObjectClass parent_class;

    gboolean (*start)(Seat *seat);
    DisplayServer *(*create_display_server) (Seat *seat, Session *session);
    gboolean (*display_server_is_used) (Seat *seat, DisplayServer *display_server);
    GreeterSession *(*create_greeter_session) (Seat *seat);
    Session *(*create_session) (Seat *seat);
    void (*set_active_session)(Seat *seat, Session *session);
    void (*set_next_session)(Seat *seat, Session *session);
    Session *(*get_active_session)(Seat *seat);
    void (*run_script)(Seat *seat, DisplayServer *display_server, Process *script);
    void (*stop)(Seat *seat);

    void (*session_added)(Seat *seat, Session *session);
    void (*running_user_session)(Seat *seat, Session *session);
    void (*session_removed)(Seat *seat, Session *session);
    void (*stopped)(Seat *seat);
} SeatClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Seat, g_object_unref)

GType seat_get_type (void);

void seat_register_module (const gchar *name, GType type);

Seat *seat_new (const gchar *module_name);

void seat_set_name (Seat *seat, const gchar *name);

void seat_set_property (Seat *seat, const gchar *name, const gchar *value);

const gchar *seat_get_string_property (Seat *seat, const gchar *name);

gchar **seat_get_string_list_property (Seat *seat, const gchar *name);

gboolean seat_get_boolean_property (Seat *seat, const gchar *name);

gint seat_get_integer_property_with_fallback (Seat *seat, const gchar *name, gint fallback);

gint seat_get_integer_property (Seat *seat, const gchar *name);

const gchar *seat_get_name (Seat *seat);

void seat_set_supports_multi_session (Seat *seat, gboolean supports_multi_session);

void seat_set_share_display_server (Seat *seat, gboolean share_display_server);

void seat_set_can_tty (Seat *seat, gboolean can_tty);

gboolean seat_start (Seat *seat);

GList *seat_get_sessions (Seat *seat);

void seat_set_active_session (Seat *seat, Session *session);

Session *seat_get_active_session (Seat *seat);

Session *seat_get_next_session (Seat *seat);

void seat_set_externally_activated_session (Seat *seat, Session *session);

Session *seat_get_expected_active_session (Seat *seat);

Session *seat_find_session_by_login1_id (Seat *seat, const gchar *login1_session_id);

gboolean seat_get_can_switch (Seat *seat);

gboolean seat_get_can_tty (Seat *seat);

gboolean seat_get_allow_guest (Seat *seat);

gboolean seat_get_greeter_allow_guest (Seat *seat);

gboolean seat_switch_to_greeter (Seat *seat);

gboolean seat_switch_to_user (Seat *seat, const gchar *username, const gchar *session_name);

gboolean seat_switch_to_guest (Seat *seat, const gchar *session_name);

gboolean seat_lock (Seat *seat, const gchar *username);

void seat_stop (Seat *seat);

gboolean seat_get_is_stopping (Seat *seat);

G_END_DECLS

#endif /* SEAT_H_ */
