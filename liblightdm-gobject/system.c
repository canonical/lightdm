/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include <sys/utsname.h>

#include "lightdm/system.h"

static gchar *hostname = NULL;

/**
 * lightdm_get_hostname:
 *
 * Return value: The name of the host we are running on.
 **/
const gchar *
lightdm_get_hostname (void)
{
    if (!hostname)
    {
        struct utsname info;
        uname (&info);
        hostname = g_strdup (info.nodename);
    }

    return hostname;
}
