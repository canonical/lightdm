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

/* for setres*id() */
#define _GNU_SOURCE

#include <config.h>
#include <glib.h>
#include <unistd.h>
#include "privileges.h"

void
privileges_drop (uid_t uid, gid_t gid)
{
#ifdef HAVE_SETRESGID
    g_assert (setresgid (gid, gid, -1) == 0);
#else
    g_assert (setgid (gid) == 0);
    g_assert (setegid (gid) == 0);
#endif
#ifdef HAVE_SETRESUID
    g_assert (setresuid (uid, uid, -1) == 0);
#else
    g_assert (setuid (uid) == 0);
    g_assert (seteuid (uid) == 0);
#endif
}

void
privileges_reclaim (void)
{
#ifdef HAVE_SETRESUID
    g_assert (setresuid (0, 0, -1) == 0);
#else
    g_assert (setuid (0) == 0);
    g_assert (seteuid (0) == 0);
#endif
#ifdef HAVE_SETRESGID
    g_assert (setresgid (0, 0, -1) == 0);
#else
    g_assert (setgid (0) == 0);
    g_assert (setegid (0) == 0);
#endif
}
