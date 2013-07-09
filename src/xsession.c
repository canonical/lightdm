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

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "xsession.h"
#include "configuration.h"
#include "privileges.h"

G_DEFINE_TYPE (XSession, xsession, SESSION_TYPE);

XSession *
xsession_new (void)
{
    XSession *session;

    session = g_object_new (XSESSION_TYPE, NULL);
    session_set_log_file (SESSION (session), ".xsession-errors");

    return session;
}

static void
xsession_set_display_server (Session *session, DisplayServer *display_server)
{
    XServer *xserver;
    XAuthority *authority;

    xserver = XSERVER (display_server);

    session_set_env (session, "DISPLAY", xserver_get_address (xserver));
    session_set_tty (session, xserver_get_address (xserver));
    session_set_xdisplay (session, xserver_get_address (xserver));
    authority = xserver_get_authority (xserver);
    if (authority)
        session_set_xauthority (session, authority, config_get_boolean (config_get_instance (), "LightDM", "user-authority-in-system-dir"));

    SESSION_CLASS (xsession_parent_class)->set_display_server (session, display_server);
}

static void
xsession_init (XSession *session)
{
}

static void
xsession_class_init (XSessionClass *klass)
{
    SessionClass *session_class = SESSION_CLASS (klass);

    session_class->set_display_server = xsession_set_display_server;
}
