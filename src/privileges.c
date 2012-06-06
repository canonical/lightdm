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
#include "privileges.h"

void
privileges_drop (User *user)
{
    g_return_if_fail (user != NULL);

    g_debug ("Dropping privileges to uid %i", user_get_uid (user));
#ifdef HAVE_SETRESGID
    g_debug ("Calling setresgid");
    g_assert (setresgid (user_get_gid (user), user_get_gid (user), -1) == 0);
#else
    g_assert (setgid (user_get_gid (user)) == 0);
    g_assert (setegid (user_get_gid (user)) == 0);
#endif
#ifdef HAVE_SETRESUID
    g_debug ("Calling setresuid");
    g_assert (setresuid (user_get_uid (user), user_get_uid (user), -1) == 0);
#else
    g_assert (setuid (user_get_uid (user)) == 0);
    g_assert (seteuid (user_get_uid (user)) == 0);
#endif
}

void
privileges_reclaim (void)
{
    g_debug ("Restoring privileges");
#ifdef HAVE_SETRESUID
    g_debug ("Calling setresuid");
    g_assert (setresuid (0, 0, -1) == 0);
#else
    g_assert (setuid (0) == 0);
    g_assert (seteuid (0) == 0);
#endif
#ifdef HAVE_SETRESGID
    g_debug ("Calling setresgid");
    g_assert (setresgid (0, 0, -1) == 0);
#else
    g_assert (setgid (0) == 0);
    g_assert (setegid (0) == 0);
#endif
}
