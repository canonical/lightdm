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
    gchar *script;
    static gchar *setup_script = NULL;
  
    if (setup_script)
        return setup_script;

    script = config_get_string (config_get_instance (), "LightDM", "guest-account-script");
    if (!script)
        return NULL;

    setup_script = g_find_program_in_path (script);
    g_free (script);
  
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
    gchar **argv;
    gboolean result;

    if (!g_shell_parse_argv (script, &argc, &argv, error))
        return FALSE;

    result = g_spawn_sync (NULL, argv, NULL,
                           G_SPAWN_SEARCH_PATH,
                           NULL, NULL,
                           stdout_text, NULL, exit_status, error);
    g_strfreev (argv);
  
    return result;
}

gchar *
guest_account_setup (void)
{
    gchar *command, *stdout_text, *username, *start, *c;
    gint exit_status;
    gboolean result;
    GError *error = NULL;

    command = g_strdup_printf ("%s add", get_setup_script ());
    g_debug ("Opening guest account with command '%s'", command);
    result = run_script (command, &stdout_text, &exit_status, &error);
    g_free (command);
    if (!result)
        g_warning ("Error running guest account setup script '%s': %s", get_setup_script (), error->message);
    g_clear_error (&error);
    if (!result)
        return NULL;

    if (exit_status != 0)
    {
        g_debug ("Guest account setup script returns %d: %s", exit_status, stdout_text);
        g_free (stdout_text);
        return NULL;
    }
  
    /* Use the first line and trim whitespace */
    start = stdout_text;
    while (isspace (*start))
       start++;
    c = start;
    while (!isspace (*c))
       c++;
    *c = '\0';
    username = g_strdup (start);
    g_free (stdout_text);

    if (strcmp (username, "") == 0)
    {
        g_free (username);
        return NULL;
    }
  
    g_debug ("Guest account %s setup", username);

    return username;
}

void
guest_account_cleanup (const gchar *username)
{
    gchar *command;
    gint exit_status;
    GError *error = NULL;

    command = g_strdup_printf ("%s remove %s", get_setup_script (), username);
    g_debug ("Closing guest account %s with command '%s'", username, command);
    if (run_script (command, NULL, &exit_status, &error))
    {
        if (exit_status != 0)
            g_debug ("Guest account cleanup script returns %d", exit_status);
    }
    else
        g_warning ("Error running guest account cleanup script '%s': %s", get_setup_script (), error->message);
    g_clear_error (&error);

    g_free (command);
}
