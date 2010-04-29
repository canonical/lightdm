/*
 * Copyright (C) 2010 Canonical Ltd.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <glib.h>

#include "display-manager.h"

int
main(int argc, char **argv)
{
    GMainLoop *loop;
    DisplayManager *manager;

    g_type_init ();
    loop = g_main_loop_new (NULL, FALSE);

    // Load config
    // FIXME: If autologin selected the first display should be a user session

    manager = display_manager_new ();

    g_main_loop_run (loop);

    return 0;
}
