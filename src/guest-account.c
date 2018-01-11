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
#include <ctype.h>

#include "guest-account.h"
#include "configuration.h"

static gchar *
get_setup_script (void)
{
    static gchar *setup_script = NULL;

    if (setup_script)
        return setup_script;

    g_autofree gchar *script = config_get_string (config_get_instance (), "LightDM", "guest-account-script");
    if (!script)
        return NULL;

    setup_script = g_find_program_in_path (script);

    return setup_script;
}

gboolean
guest_account_is_installed (void)
{
    return get_setup_script () != NULL;
}

static gboolean
run_script (const gchar *script, gchar **stdout_text, gint *exit_status, GError **error)
{
    gint argc;
    g_auto(GStrv) argv = NULL;
    if (!g_shell_parse_argv (script, &argc, &argv, error))
        return FALSE;

    gboolean result = g_spawn_sync (NULL, argv, NULL,
                                    G_SPAWN_SEARCH_PATH,
                                    NULL, NULL,
                                    stdout_text, NULL, exit_status, error);

    return result;
}

gchar *
guest_account_setup (void)
{
    g_autofree gchar *command = g_strdup_printf ("%s add", get_setup_script ());
    g_debug ("Opening guest account with command '%s'", command);
    g_autofree gchar *stdout_text = NULL;
    gint exit_status;
    g_autoptr(GError) error = NULL;
    gboolean result = run_script (command, &stdout_text, &exit_status, &error);
    if (error)
        g_warning ("Error running guest account setup script '%s': %s", get_setup_script (), error->message);
    if (!result)
        return NULL;

    if (exit_status != 0)
    {
        g_debug ("Guest account setup script returns %d: %s", exit_status, stdout_text);
        return NULL;
    }

    /* Use the last line and trim whitespace */
    g_auto(GStrv) lines = g_strsplit (g_strstrip (stdout_text), "\n", -1);
    g_autofree gchar *username = NULL;
    if (lines)
        username = g_strdup (g_strstrip (lines[g_strv_length (lines) - 1]));
    else
        username = g_strdup ("");

    if (strcmp (username, "") == 0)
    {
        g_debug ("Guest account setup script didn't return a username");
        return NULL;
    }

    g_debug ("Guest account %s setup", username);

    return g_steal_pointer (&username);
}

void
guest_account_cleanup (const gchar *username)
{
    g_autofree gchar *command = g_strdup_printf ("%s remove %s", get_setup_script (), username);
    g_debug ("Closing guest account %s with command '%s'", username, command);

    gint exit_status;
    g_autoptr(GError) error = NULL;
    gboolean result = run_script (command, NULL, &exit_status, &error);

    if (error)
        g_warning ("Error running guest account cleanup script '%s': %s", get_setup_script (), error->message);

    if (result && exit_status != 0)
        g_debug ("Guest account cleanup script returns %d", exit_status);
}
