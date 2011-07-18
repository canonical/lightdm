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

#include <config.h>

#include "xdisplay.h"
#include "xsession.h"

G_DEFINE_TYPE (XDisplay, xdisplay, DISPLAY_TYPE);

XDisplay *
xdisplay_new (const gchar *config_section, XServer *server)
{
    XDisplay *self = g_object_new (XDISPLAY_TYPE, NULL);

    g_return_val_if_fail (server != NULL, NULL);

    display_set_display_server (DISPLAY (self), DISPLAY_SERVER (server));

    return self;
}

static Session *
xdisplay_create_session (Display *display)
{
    return SESSION (xsession_new (XSERVER (display_get_display_server (display))));
}

static void
xdisplay_init (XDisplay *display)
{
}

static void
xdisplay_class_init (XDisplayClass *klass)
{
    DisplayClass *display_class = DISPLAY_CLASS (klass);

    display_class->create_session = xdisplay_create_session;
}
