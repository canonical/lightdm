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
#include <signal.h>
#include <unistd.h>

#include "display-manager.h"

static GMainLoop *loop;

static void
signal_handler (int signum)
{
    g_debug ("Caught %s signal, exiting", g_strsignal (signum));
    g_main_loop_quit (loop);
}

int
main(int argc, char **argv)
{
    DisplayManager *manager;
    struct sigaction action;

    /* Quit cleanly on signals */
    action.sa_handler = signal_handler;
    sigemptyset (&action.sa_mask);
    action.sa_flags = 0;
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGHUP, &action, NULL);

    g_type_init ();

    if (getuid () != 0)
    {
        g_print ("Only root can run Light Display Manager\n");
        return -1;
    }

    g_debug ("Starting Light Display Manager %s, PID=%i", VERSION, getpid ());

    // Change working directory?

    loop = g_main_loop_new (NULL, FALSE);

    // Load config
    // FIXME: If autologin selected the first display should be a user session

    manager = display_manager_new ();

    g_main_loop_run (loop);

    return 0;
}
