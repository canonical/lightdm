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

#include "privileges.h"

void
privileges_drop (User *user)
{
    if (geteuid () != 0 || user == NULL)
        return;

    g_debug ("Dropping privileges to uid %i", user_get_uid (user));
    g_assert (setresgid (user_get_gid (user), user_get_gid (user), -1) == 0);
    g_assert (setresuid (user_get_uid (user), user_get_uid (user), -1) == 0);
}

void
privileges_reclaim (void)
{
    if (geteuid () != 0)
        return;
  
    g_debug ("Restoring privileges");
    g_assert (setresuid (0, 0, -1) == 0);
    g_assert (setresgid (0, 0, -1) == 0);  
}
