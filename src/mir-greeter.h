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

#ifndef MIR_GREETER_H_
#define MIR_GREETER_H_

#include "greeter.h"

G_BEGIN_DECLS

#define MIR_GREETER_TYPE (mir_greeter_get_type())
#define MIR_GREETER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), MIR_GREETER_TYPE, MirGreeter))

typedef struct MirGreeterPrivate MirGreeterPrivate;

typedef struct
{
    Greeter          parent_instance;
    MirGreeterPrivate *priv;
} MirGreeter;

typedef struct
{
    GreeterClass parent_class;
} MirGreeterClass;

GType mir_greeter_get_type (void);

MirGreeter *mir_greeter_new (void);

G_END_DECLS

#endif /* MIR_GREETER_H_ */
