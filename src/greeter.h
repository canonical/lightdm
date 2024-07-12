/*
 * Copyright (C) 2010-2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef GREETER_H_
#define GREETER_H_

typedef struct Greeter Greeter;

#include "session.h"

G_BEGIN_DECLS

#define GREETER_TYPE           (greeter_get_type())
#define GREETER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), GREETER_TYPE, Greeter))
#define GREETER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), GREETER_TYPE, GreeterClass))
#define GREETER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GREETER_TYPE, GreeterClass))
#define IS_GREETER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GREETER_TYPE))

#define GREETER_SIGNAL_CONNECTED      "connected"
#define GREETER_SIGNAL_DISCONNECTED   "disconnected"
#define GREETER_SIGNAL_CREATE_SESSION "create-session"
#define GREETER_SIGNAL_START_SESSION  "start-session"

#define GREETER_PROPERTY_ACTIVE_USERNAME "active-username"

#define GREETER_SIGNAL_ACTIVE_USERNAME_CHANGED "notify::" GREETER_PROPERTY_ACTIVE_USERNAME

struct Greeter
{
    GObject parent_instance;
};

typedef struct
{
    GObjectClass parent_class;
    void (*connected)(Greeter *greeter);
    void (*disconnected)(Greeter *greeter);  
    Session *(*create_session)(Greeter *greeter);
    gboolean (*start_session)(Greeter *greeter, SessionType type, const gchar *session);
} GreeterClass;

typedef enum
{
    BEHAVIOR_IMMEDIATE,
    BEHAVIOR_RESETTABLE,
    BEHAVIOR_GRACEFUL,
    LAST_BEHAVIOR,
} FinishBehavior;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Greeter, g_object_unref)

GType greeter_get_type (void);

Greeter *greeter_new (void);

void greeter_set_file_descriptors (Greeter *greeter, int to_greeter_fd, int from_greeter_fd);

void greeter_stop (Greeter *greeter);

void greeter_set_pam_services (Greeter *greeter, const gchar *pam_service, const gchar *autologin_pam_service);

void greeter_set_allow_guest (Greeter *greeter, gboolean allow_guest);

void greeter_clear_hints (Greeter *greeter);

void greeter_set_hint (Greeter *greeter, const gchar *name, const gchar *value);

void greeter_idle (Greeter *greeter);

void greeter_reset (Greeter *greeter);

gboolean greeter_get_guest_authenticated (Greeter *greeter);

Session *greeter_take_authentication_session (Greeter *greeter);

gboolean greeter_get_start_session (Greeter *greeter);

FinishBehavior greeter_get_finish_behavior (Greeter *greeter);

const gchar *greeter_get_active_username (Greeter *greeter);

G_END_DECLS

#endif /* GREETER_H_ */
