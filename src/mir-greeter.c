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

#include "mir-greeter.h"

G_DEFINE_TYPE (MirGreeter, mir_greeter, GREETER_TYPE);

MirGreeter *
mir_greeter_new (void)
{
    return g_object_new (MIR_GREETER_TYPE, NULL);
}

static void
mir_greeter_init (MirGreeter *session)
{
}

static void
mir_greeter_class_init (MirGreeterClass *klass)
{
}
