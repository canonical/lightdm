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
#include <sys/wait.h>

#include "plymouth.h"

static gboolean have_pinged = FALSE;
static gboolean have_checked_active_vt = FALSE;

static gboolean is_running = FALSE;
static gboolean is_active = FALSE;
static gboolean has_active_vt = FALSE;

static gboolean
plymouth_run_command (const gchar *command, gint *exit_status)
{
    g_autofree gchar *command_line = g_strdup_printf ("plymouth %s", command);
    g_autoptr(GError) error = NULL;
    gboolean result = g_spawn_command_line_sync (command_line, NULL, NULL, exit_status, &error);

    if (error)
        g_debug ("Could not run %s: %s", command_line, error->message);

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
plymouth_get_is_running (void)
{
    if (!have_pinged)
    {
        have_pinged = TRUE;
        is_running = plymouth_command_returns_true ("--ping");
        is_active = is_running;
    }

    return is_running;
}

gboolean
plymouth_get_is_active (void)
{
    return plymouth_get_is_running () && is_active;
}

gboolean
plymouth_has_active_vt (void)
{
    if (!have_checked_active_vt)
    {
        have_checked_active_vt = TRUE;
        has_active_vt = plymouth_command_returns_true ("--has-active-vt");
    }

    return has_active_vt;
}

void
plymouth_deactivate (void)
{
    g_debug ("Deactivating Plymouth");
    is_active = FALSE;
    plymouth_run_command ("deactivate", NULL);
}

void
plymouth_quit (gboolean retain_splash)
{
    if (retain_splash)
        g_debug ("Quitting Plymouth; retaining splash");
    else
        g_debug ("Quitting Plymouth");

    have_pinged = TRUE;
    is_running = FALSE;
    if (retain_splash)
        plymouth_run_command ("quit --retain-splash", NULL);
    else
        plymouth_run_command ("quit", NULL);
}
