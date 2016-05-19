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

#ifndef GREETER_SESSION_H_
#define GREETER_SESSION_H_

#include "session.h"
#include "greeter.h"

G_BEGIN_DECLS

#define GREETER_SESSION_TYPE           (greeter_session_get_type())
#define GREETER_SESSION(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), GREETER_SESSION_TYPE, GreeterSession))
#define GREETER_SESSION_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), GREETER_SESSION_TYPE, GreeterSessionClass))
#define GREETER_SESSION_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GREETER_SESSION_TYPE, GreeterSessionClass))
#define IS_GREETER_SESSION(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GREETER_SESSION_TYPE))

typedef struct GreeterSessionPrivate GreeterSessionPrivate;

typedef struct
{
    Session                parent_instance;
    GreeterSessionPrivate *priv;
} GreeterSession;

typedef struct
{
    SessionClass parent_class;
} GreeterSessionClass;

GType greeter_session_get_type (void);

GreeterSession *greeter_session_new (void);

Greeter *greeter_session_get_greeter (GreeterSession *session);

G_END_DECLS

#endif /* GREETER_SESSION_H_ */
