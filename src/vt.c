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

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/vt.h>
#endif

#include "vt.h"
#include "configuration.h"

static GList *used_vts = NULL;

static gint
open_console (void)
{
    int fd;

    if (getuid () != 0)
        return -1;

    fd = g_open ("/dev/console", O_RDONLY | O_NOCTTY);
    if (fd < 0)
        g_warning ("Error opening /dev/console: %s", strerror (errno));
    return fd;
}

gint
vt_get_active (void)
{
#ifdef __linux__
    gint console_fd;
    gint active = -1;

    console_fd = open_console ();
    if (console_fd >= 0)
    {
        struct vt_stat console_state = { 0 };
        if (ioctl (console_fd, VT_GETSTATE, &console_state) < 0)
            g_warning ("Error using VT_GETSTATE on /dev/console: %s", strerror (errno));
        else
            active = console_state.v_active;
        close (console_fd);
    }

    return active;
#else
    return -1;
#endif
}

void
vt_set_active (gint number)
{
#ifdef __linux__
    gint console_fd;

    g_debug ("Activating VT %d", number);

    console_fd = open_console ();
    if (console_fd >= 0)
    {
        int n = number;
        if (ioctl (console_fd, VT_ACTIVATE, n) < 0)
            g_warning ("Error using VT_ACTIVATE %d on /dev/console: %s", n, strerror (errno));
        close (console_fd);
    }
#endif
}

static gboolean
vt_is_used (gint number)
{
    GList *link;

    for (link = used_vts; link; link = link->next)
    {
        int n = GPOINTER_TO_INT (link->data);
        if (n == number)
            return TRUE;
    }

    return FALSE;
}

gint
vt_get_min (void)
{
    gint number;

    number = config_get_integer (config_get_instance (), "LightDM", "minimum-vt");
    if (number < 1)
        number = 1;

    return number;
}

gint
vt_get_unused (void)
{
    gint number;

    number = vt_get_min ();
    while (vt_is_used (number))
        number++;

    used_vts = g_list_append (used_vts, GINT_TO_POINTER (number));
 
    return number;
}

void
vt_release (gint number)
{
    used_vts = g_list_remove (used_vts, GINT_TO_POINTER (link));
}
