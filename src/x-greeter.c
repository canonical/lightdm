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

#include "x-greeter.h"
#include "x-server.h"
#include "configuration.h"

G_DEFINE_TYPE (XGreeter, x_greeter, GREETER_TYPE);

XGreeter *
x_greeter_new (void)
{
    return g_object_new (XGREETER_TYPE, NULL);
}

static void
setup_env (XGreeter *greeter)
{
    DisplayServer *display_server;
    gint vt;

    display_server = session_get_display_server (SESSION (greeter));

    vt = display_server_get_vt (display_server);
    if (vt > 0)
    {
        gchar *t;

        t = g_strdup_printf ("/dev/tty%d", vt);
        session_set_tty (SESSION (greeter), t);
        g_free (t);

        t = g_strdup_printf ("%d", vt);
        session_set_env (SESSION (greeter), "XDG_VTNR", t);
        g_free (t);
    }

    session_set_env (SESSION (greeter), "DISPLAY", x_server_get_address (X_SERVER (display_server)));
    session_set_tty (SESSION (greeter), x_server_get_address (X_SERVER (display_server)));
    session_set_xdisplay (SESSION (greeter), x_server_get_address (X_SERVER (display_server)));
    session_set_remote_host_name (SESSION (greeter), x_server_get_hostname (X_SERVER (display_server)));
    session_set_x_authority (SESSION (greeter),
                             x_server_get_authority (X_SERVER (display_server)),
                             config_get_boolean (config_get_instance (), "LightDM", "user-authority-in-system-dir"));
}

static gboolean
x_greeter_start (Session *session)
{
    setup_env (XGREETER (session));
    return SESSION_CLASS (x_greeter_parent_class)->start (session);
}

static void
x_greeter_run (Session *session)
{
    setup_env (XGREETER (session));
    SESSION_CLASS (x_greeter_parent_class)->run (session);
}

static void
x_greeter_init (XGreeter *session)
{
}

static void
x_greeter_class_init (XGreeterClass *klass)
{
    SessionClass *session_class = SESSION_CLASS (klass);

    session_class->start = x_greeter_start;
    session_class->run = x_greeter_run;
}
