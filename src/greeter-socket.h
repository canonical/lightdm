/*
 * Copyright (C) 2010-2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef GREETER_SOCKET_H_
#define GREETER_SOCKET_H_

#include <glib-object.h>

#include "greeter.h"

G_BEGIN_DECLS

#define GREETER_SOCKET_TYPE           (greeter_socket_get_type())
#define GREETER_SOCKET(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), GREETER_SOCKET_TYPE, GreeterSocket))
#define GREETER_SOCKET_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), GREETER_SOCKET_TYPE, GreeterSocketClass))
#define GREETER_SOCKET_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GREETER_SOCKET_TYPE, GreeterSocketClass))
#define IS_GREETER_SOCKET(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GREETER_SOCKET_TYPE))

#define GREETER_SOCKET_SIGNAL_CREATE_GREETER "create-greeter"

typedef struct
{
    GObject parent_instance;
} GreeterSocket;

typedef struct
{
    GObjectClass parent_class;
    Greeter *(*create_greeter)(GreeterSocket *socket);
} GreeterSocketClass;

GType greeter_socket_get_type (void);

GreeterSocket *greeter_socket_new (const gchar *path);

gboolean greeter_socket_start (GreeterSocket *socket, GError **error);

G_END_DECLS

#endif /* GREETER_SOCKET_H_ */
