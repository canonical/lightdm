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

#ifndef _LOGIN1_H_
#define _LOGIN1_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LOGIN1_SEAT_TYPE (login1_seat_get_type())
#define LOGIN1_SEAT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LOGIN1_SEAT_TYPE, Login1Seat));

#define LOGIN1_SERVICE_TYPE (login1_service_get_type())
#define LOGIN1_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LOGIN1_SERVICE_TYPE, Login1Service));

#define LOGIN1_SERVICE_SIGNAL_SEAT_ADDED   "seat-added"
#define LOGIN1_SERVICE_SIGNAL_SEAT_REMOVED "seat-removed"
#define LOGIN1_SERVICE_SIGNAL_SEAT_ATTENTION_KEY "seat-attention-key"

#define LOGIN1_SEAT_SIGNAL_CAN_GRAPHICAL_CHANGED "can-graphical-changed"
#define LOGIN1_SIGNAL_ACTIVE_SESION_CHANGED "active-session-changed"

typedef struct
{
    GObject parent_instance;
} Login1Seat;

typedef struct
{
    GObjectClass parent_class;
    void (*can_graphical_changed)(Login1Seat *seat);
    void (*active_session_changed)(Login1Seat *seat, const gchar *login1_session_id);
} Login1SeatClass;

typedef struct
{
    GObject parent_instance;
} Login1Service;

typedef struct
{
    GObjectClass parent_class;
    void (*seat_added)(Login1Service *service, Login1Seat *seat);
    void (*seat_removed)(Login1Service *service, Login1Seat *seat);
    void (*seat_attention_key)(Login1Service *service, Login1Seat *seat);
} Login1ServiceClass;

GType login1_service_get_type (void);

GType login1_seat_get_type (void);

Login1Service *login1_service_get_instance (void);

gboolean login1_service_connect (Login1Service *service);

gboolean login1_service_get_is_connected (Login1Service *service);

GList *login1_service_get_seats (Login1Service *service);

Login1Seat *login1_service_get_seat (Login1Service *service, const gchar *id);

void login1_service_lock_session (Login1Service *service, const gchar *session_id);

void login1_service_unlock_session (Login1Service *service, const gchar *session_id);

void login1_service_activate_session (Login1Service *service, const gchar *session_id);

void login1_service_terminate_session (Login1Service *service, const gchar *session_id);

const gchar *login1_seat_get_id (Login1Seat *seat);

gboolean login1_seat_get_can_graphical (Login1Seat *seat);

gboolean login1_seat_get_can_multi_session (Login1Seat *seat);

gboolean login1_seat_get_can_tty (Login1Seat *seat);

G_END_DECLS

#endif /* _LOGIN1_H_ */
