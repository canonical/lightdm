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

#include "mir-session.h"
#include "mir-server.h"

G_DEFINE_TYPE (MirSession, mir_session, SESSION_TYPE);

MirSession *
mir_session_new (void)
{
    MirSession *session;

    session = g_object_new (MIR_SESSION_TYPE, NULL);
    session_set_log_file (SESSION (session), ".session-errors");

    return session;
}

static void
mir_session_set_display_server (Session *session, DisplayServer *display_server)
{
    MirServer *mir_server;

    mir_server = MIR_SERVER (display_server);

    SESSION_CLASS (mir_session_parent_class)->set_display_server (session, display_server);
}

static void
mir_session_init (MirSession *session)
{
}

static void
mir_session_class_init (MirSessionClass *klass)
{
    SessionClass *session_class = SESSION_CLASS (klass);

    session_class->set_display_server = mir_session_set_display_server;
}
