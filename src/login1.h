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

typedef struct Login1SeatPrivate Login1SeatPrivate;

typedef struct
{
    GObject            parent_instance;
    Login1SeatPrivate *priv;
} Login1Seat;

typedef struct
{
    GObjectClass parent_class;
    void (*can_graphical_changed)(Login1Seat *seat);
} Login1SeatClass;

typedef struct Login1ServicePrivate Login1ServicePrivate;

typedef struct
{
    GObject               parent_instance;
    Login1ServicePrivate *priv;
} Login1Service;

typedef struct
{
    GObjectClass parent_class;
    void (*seat_added)(Login1Service *service, Login1Seat *seat);
    void (*seat_removed)(Login1Service *service, Login1Seat *seat);
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

const gchar *login1_seat_get_id (Login1Seat *seat);

gboolean login1_seat_get_can_graphical (Login1Seat *seat);

gboolean login1_seat_get_can_multi_session (Login1Seat *seat);

G_END_DECLS

#endif /* _LOGIN1_H_ */
