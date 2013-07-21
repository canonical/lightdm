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

#ifndef GREETER_H_
#define GREETER_H_

#include "session.h"

G_BEGIN_DECLS

#define GREETER_TYPE           (greeter_get_type())
#define GREETER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), GREETER_TYPE, Greeter))
#define GREETER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), GREETER_TYPE, GreeterClass))
#define GREETER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GREETER_TYPE, GreeterClass))
#define IS_GREETER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GREETER_TYPE))

typedef struct GreeterPrivate GreeterPrivate;

typedef struct
{
    Session         parent_instance;
    GreeterPrivate *priv;
} Greeter;

typedef struct
{
    SessionClass parent_class;
    void (*connected)(Greeter *greeter);
    Session *(*create_session)(Greeter *greeter);
    gboolean (*start_session)(Greeter *greeter, SessionType type, const gchar *session);
} GreeterClass;

GType greeter_get_type (void);

void greeter_set_pam_services (Greeter *greeter, const gchar *pam_service, const gchar *autologin_pam_service);

void greeter_set_allow_guest (Greeter *greeter, gboolean allow_guest);

void greeter_set_hint (Greeter *greeter, const gchar *name, const gchar *value);

gboolean greeter_get_guest_authenticated (Greeter *greeter);

Session *greeter_get_authentication_session (Greeter *greeter);

gboolean greeter_get_start_session (Greeter *greeter);

G_END_DECLS

#endif /* GREETER_H_ */
