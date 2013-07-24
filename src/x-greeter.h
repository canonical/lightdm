/*
 * Copyright (C) 2013 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef XGREETER_H_
#define XGREETER_H_

#include "greeter.h"

G_BEGIN_DECLS

#define XGREETER_TYPE (x_greeter_get_type())
#define XGREETER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XGREETER_TYPE, XGreeter))

typedef struct XGreeterPrivate XGreeterPrivate;

typedef struct
{
    Greeter          parent_instance;
    XGreeterPrivate *priv;
} XGreeter;

typedef struct
{
    GreeterClass parent_class;
} XGreeterClass;

GType x_greeter_get_type (void);

XGreeter *x_greeter_new (void);

G_END_DECLS

#endif /* XGREETER_H_ */
