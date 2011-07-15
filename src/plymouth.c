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

#include <stdlib.h>

#include "plymouth.h"

static gboolean
plymouth_run_command (const gchar *command, gint *exit_status)
{
    gchar *command_line;
    gboolean result;
    GError *error = NULL;

    command_line = g_strdup_printf ("/bin/plymouth %s", command);  
    result = g_spawn_command_line_sync (command_line, NULL, NULL, exit_status, &error);
    g_free (command_line);

    if (!result)
        g_debug ("Could not run %s: %s", command_line, error->message);
    g_clear_error (&error);

    return result;
}

static gboolean
plymouth_command_returns_true (gchar *command)
{
    gint exit_status;
    if (!plymouth_run_command (command, &exit_status))
        return FALSE;
    return WIFEXITED (exit_status) && WEXITSTATUS (exit_status) == 0;
}

gboolean
plymouth_is_running (void)
{
    return plymouth_command_returns_true ("--ping");
}

gboolean
plymouth_has_active_vt (void)
{
    return plymouth_command_returns_true ("--has-active-vt");
}

void
plymouth_deactivate (void)
{
    plymouth_run_command ("deactivate", NULL);
}

void
plymouth_quit (gboolean retain_splash)
{
    if (retain_splash)
        plymouth_run_command ("quit --retain-splash", NULL);
    else
        plymouth_run_command ("quit", NULL);
}
