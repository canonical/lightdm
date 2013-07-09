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

#ifndef MIR_SESSION_H_
#define MIR_SESSION_H_

#include "session.h"
#include "mir-server.h"

G_BEGIN_DECLS

#define MIR_SESSION_TYPE (mir_session_get_type())
#define MIR_SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), MIR_SESSION_TYPE, MirSession))

typedef struct MirSessionPrivate MirSessionPrivate;

typedef struct
{
    Session            parent_instance;
    MirSessionPrivate *priv;
} MirSession;

typedef struct
{
    SessionClass parent_class;
} MirSessionClass;

GType mir_session_get_type (void);

MirSession *mir_session_new (MirServer *mir_server);

G_END_DECLS

#endif /* MIR_SESSION_H_ */
