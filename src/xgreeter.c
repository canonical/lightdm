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
setup_env (XGreeter *xgreeter)
{
    DisplayServer *display_server;
    gint vt;

    display_server = session_get_display_server (SESSION (xgreeter));

    vt = display_server_get_vt (display_server);
    if (vt > 0)
    {
        gchar *t;

        t = g_strdup_printf ("/dev/tty%d", vt);
        session_set_tty (SESSION (xgreeter), t);
        g_free (t);

        t = g_strdup_printf ("%d", vt);
        session_set_env (SESSION (xgreeter), "XDG_VTNR", t);
        g_free (t);
    }

    session_set_env (SESSION (xgreeter), "DISPLAY", xserver_get_address (XSERVER (display_server)));
    session_set_tty (SESSION (xgreeter), xserver_get_address (XSERVER (display_server)));
    session_set_xdisplay (SESSION (xgreeter), xserver_get_address (XSERVER (display_server)));
    session_set_remote_host_name (SESSION (xgreeter), xserver_get_hostname (XSERVER (display_server)));
    session_set_xauthority (SESSION (xgreeter),
                            xserver_get_authority (XSERVER (display_server)),
                            config_get_boolean (config_get_instance (), "LightDM", "user-authority-in-system-dir"));
}

static gboolean
xgreeter_start (Session *session)
{
    setup_env (XGREETER (session));
    return SESSION_CLASS (xgreeter_parent_class)->start (session);
}

static void
xgreeter_run (Session *session)
{
    setup_env (XGREETER (session));
    SESSION_CLASS (xgreeter_parent_class)->run (session);
}

static void
xgreeter_init (XGreeter *session)
{
}

static void
xgreeter_class_init (XGreeterClass *klass)
{
    SessionClass *session_class = SESSION_CLASS (klass);

    session_class->start = xgreeter_start;
    session_class->run = xgreeter_run;
}
