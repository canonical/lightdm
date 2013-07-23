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
#include "xserver.h"
#include "configuration.h"

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
setup_env (XSession *xsession)
{
    DisplayServer *display_server;
    gint vt;

    display_server = session_get_display_server (SESSION (xsession));

    vt = display_server_get_vt (display_server);
    if (vt > 0)
    {
        gchar *t;

        t = g_strdup_printf ("/dev/tty%d", vt);
        session_set_tty (SESSION (xsession), t);
        g_free (t);

        t = g_strdup_printf ("%d", vt);
        session_set_env (SESSION (xsession), "XDG_VTNR", t);
        g_free (t);
    }

    session_set_env (SESSION (xsession), "DISPLAY", xserver_get_address (XSERVER (display_server)));
    session_set_tty (SESSION (xsession), xserver_get_address (XSERVER (display_server)));
    session_set_xdisplay (SESSION (xsession), xserver_get_address (XSERVER (display_server)));
    session_set_remote_host_name (SESSION (xsession), xserver_get_hostname (XSERVER (display_server)));
    session_set_xauthority (SESSION (xsession),
                            xserver_get_authority (XSERVER (display_server)),
                            config_get_boolean (config_get_instance (), "LightDM", "user-authority-in-system-dir"));
}

static gboolean
xsession_start (Session *session)
{
    setup_env (XSESSION (session));
    return SESSION_CLASS (xsession_parent_class)->start (session);
}

static void
xsession_run (Session *session)
{
    setup_env (XSESSION (session));
    SESSION_CLASS (xsession_parent_class)->run (session);
}

static void
xsession_init (XSession *session)
{
}

static void
xsession_class_init (XSessionClass *klass)
{
    SessionClass *session_class = SESSION_CLASS (klass);

    session_class->start = xsession_start;
    session_class->run = xsession_run;
}
