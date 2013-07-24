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

#include "x-session.h"
#include "x-server.h"
#include "configuration.h"

G_DEFINE_TYPE (XSession, x_session, SESSION_TYPE);

XSession *
x_session_new (void)
{
    XSession *session;

    session = g_object_new (XSESSION_TYPE, NULL);
    session_set_log_file (SESSION (session), ".xsession-errors");

    return session;
}

static void
setup_env (XSession *session)
{
    DisplayServer *display_server;
    gint vt;

    display_server = session_get_display_server (SESSION (session));

    vt = display_server_get_vt (display_server);
    if (vt > 0)
    {
        gchar *t;

        t = g_strdup_printf ("/dev/tty%d", vt);
        session_set_tty (SESSION (session), t);
        g_free (t);

        t = g_strdup_printf ("%d", vt);
        session_set_env (SESSION (session), "XDG_VTNR", t);
        g_free (t);
    }

    session_set_env (SESSION (session), "DISPLAY", x_server_get_address (X_SERVER (display_server)));
    session_set_tty (SESSION (session), x_server_get_address (X_SERVER (display_server)));
    session_set_xdisplay (SESSION (session), x_server_get_address (X_SERVER (display_server)));
    session_set_remote_host_name (SESSION (session), x_server_get_hostname (X_SERVER (display_server)));
    session_set_x_authority (SESSION (session),
                             x_server_get_authority (X_SERVER (display_server)),
                             config_get_boolean (config_get_instance (), "LightDM", "user-authority-in-system-dir"));
}

static gboolean
x_session_start (Session *session)
{
    setup_env (XSESSION (session));
    return SESSION_CLASS (x_session_parent_class)->start (session);
}

static void
x_session_run (Session *session)
{
    setup_env (XSESSION (session));
    SESSION_CLASS (x_session_parent_class)->run (session);
}

static void
x_session_init (XSession *session)
{
}

static void
x_session_class_init (XSessionClass *klass)
{
    SessionClass *session_class = SESSION_CLASS (klass);

    session_class->start = x_session_start;
    session_class->run = x_session_run;
}
