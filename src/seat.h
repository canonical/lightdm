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
#include "greeter.h"
#include "session.h"
#include "process.h"
#include "logger.h"

G_BEGIN_DECLS

#define SEAT_TYPE           (seat_get_type())
#define SEAT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_TYPE, Seat))
#define SEAT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), SEAT_TYPE, SeatClass))
#define SEAT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SEAT_TYPE, SeatClass))

typedef struct SeatPrivate SeatPrivate;

typedef struct
{
    GObject      parent_instance;
    SeatPrivate *priv;
} Seat;

typedef struct
{
    GObjectClass parent_class;

    void (*setup)(Seat *seat);
    gboolean (*start)(Seat *seat);
    DisplayServer *(*create_display_server) (Seat *seat, const gchar *session_type);
    gboolean (*display_server_supports_session_type) (Seat *seat, DisplayServer *display_server, const gchar *session_type);
    Greeter *(*create_greeter_session) (Seat *seat);
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

GType seat_get_type (void);

void seat_register_module (const gchar *name, GType type);

Seat *seat_new (const gchar *module_name);

void seat_set_property (Seat *seat, const gchar *name, const gchar *value);

const gchar *seat_get_string_property (Seat *seat, const gchar *name);

gchar **seat_get_string_list_property (Seat *seat, const gchar *name);

gboolean seat_get_boolean_property (Seat *seat, const gchar *name);

gint seat_get_integer_property (Seat *seat, const gchar *name);

const gchar *seat_get_name (Seat *seat);

void seat_set_supports_multi_session (Seat *seat, gboolean supports_multi_session);

void seat_set_share_display_server (Seat *seat, gboolean share_display_server);

gboolean seat_start (Seat *seat);

GList *seat_get_sessions (Seat *seat);

void seat_set_active_session (Seat *seat, Session *session);

Session *seat_get_active_session (Seat *seat);

Session *seat_get_next_session (Seat *seat);

gboolean seat_get_can_switch (Seat *seat);

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
