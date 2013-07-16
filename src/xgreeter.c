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

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "xgreeter.h"
#include "xserver.h"
#include "configuration.h"

G_DEFINE_TYPE (XGreeter, xgreeter, GREETER_TYPE);

XGreeter *
xgreeter_new (void)
{
    return g_object_new (XGREETER_TYPE, NULL);
}

static void
xgreeter_set_display_server (Session *session, DisplayServer *display_server)
{
    XServer *xserver;
    gint vt;

    xserver = XSERVER (display_server);

    vt = display_server_get_vt (display_server);
    if (vt > 0)
    {
        gchar *t;

        t = g_strdup_printf ("/dev/tty%d", vt);
        session_set_tty (session, t);
        g_free (t);

        t = g_strdup_printf ("%d", vt);
        session_set_env (session, "XDG_VTNR", t);
        g_free (t);
    }

    session_set_env (session, "DISPLAY", xserver_get_address (xserver));
    session_set_tty (session, xserver_get_address (xserver));
    session_set_xdisplay (session, xserver_get_address (xserver));
    session_set_remote_host_name (session, xserver_get_hostname (xserver));
    session_set_xauthority (session,
                            xserver_get_authority (xserver),
                            config_get_boolean (config_get_instance (), "LightDM", "user-authority-in-system-dir"));

    SESSION_CLASS (xgreeter_parent_class)->set_display_server (session, display_server);
}

static void
xgreeter_init (XGreeter *session)
{
}

static void
xgreeter_class_init (XGreeterClass *klass)
{
    SessionClass *session_class = SESSION_CLASS (klass);

    session_class->set_display_server = xgreeter_set_display_server;
}
