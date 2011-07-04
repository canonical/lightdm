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

gint
vt_get_active (void)
{
#ifdef __linux__
    gint console_fd;
    struct vt_stat console_state = { 0 };    

    console_fd = g_open ("/dev/console", O_RDONLY | O_NOCTTY);
    if (console_fd < 0)
    {
        g_warning ("Error opening /dev/console: %s", strerror (errno));
        return -1;
    }

    if (ioctl (console_fd, VT_GETSTATE, &console_state) < 0)
        g_warning ("Error using VT_GETSTATE on /dev/console: %s", strerror (errno));

    close (console_fd);

    return console_state.v_active;
#else
    return -1;
#endif    
}

gint
vt_get_unused (void)
{
#ifdef __linux__
    gint console_fd;
    int number;

    console_fd = g_open ("/dev/console", O_RDONLY | O_NOCTTY);
    if (console_fd < 0)
    {
        g_warning ("Error opening /dev/console: %s", strerror (errno));
        return -1;
    }

    if (ioctl (console_fd, VT_OPENQRY, &number) < 0)
        g_warning ("Error using VT_OPENQRY on /dev/console: %s", strerror (errno));

    close (console_fd);
  
    return number;
#else
    return -1;
#endif    
}

void
vt_release (gint number)
{
}
