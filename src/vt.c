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

    fd = g_open ("/dev/console", O_RDONLY | O_NOCTTY, 0);
    if (fd < 0)
        g_warning ("Error opening /dev/console: %s", strerror (errno));
    return fd;
}

gboolean
vt_can_multi_seat (void)
{
    /* Quick check to see if we can multi seat.  This is intentionally the
       same check logind does, just without actually reading from the files.
       Existence will prove whether we have CONFIG_VT built into the kernel.
       (Reading /dev/console like the rest of the code in this file isn't
       sufficient -- it may still exist if tty0 doesn't and it may not work
       in situations where tty0 does exist and thus logind will think we are
       multi seat.) */
    return access ("/dev/tty0", F_OK) == 0 &&
           access ("/sys/class/tty/tty0/active", F_OK) == 0;
}

gint
vt_get_active (void)
{
#ifdef __linux__
    gint console_fd;
    gint active = -1;

    /* Pretend always active */
    if (getuid () != 0)
        return 1;

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

    /* Pretend always active */
    if (getuid () != 0)
        return;

    console_fd = open_console ();
    if (console_fd >= 0)
    {
        int n = number;

        if (ioctl (console_fd, VT_ACTIVATE, n) < 0)
            g_warning ("Error using VT_ACTIVATE %d on /dev/console: %s", n, strerror (errno));

        /* Wait for the VT to become active to avoid a suspected
         * race condition somewhere between LightDM, X, ConsoleKit and the kernel.
         * See https://bugs.launchpad.net/bugs/851612 */
        if (ioctl (console_fd, VT_WAITACTIVE) < 0)
            g_warning ("Error using VT_WAITACTIVE %d on /dev/console: %s", n, strerror (errno));

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

    if (getuid () != 0)
        return -1;

    number = vt_get_min ();
    while (vt_is_used (number))
        number++;

    return number;
}

void
vt_ref (gint number)
{
    g_debug ("Using VT %d", number);
    used_vts = g_list_append (used_vts, GINT_TO_POINTER (number));
}

void
vt_unref (gint number)
{
    g_debug ("Releasing VT %d", number);
    used_vts = g_list_remove (used_vts, GINT_TO_POINTER (number));
}
