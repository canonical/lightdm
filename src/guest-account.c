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

#include "guest-account.h"
#include "configuration.h"

/* Reference count */
static gint ref_count = 0;

/* Username of guest account */
static gchar *username = NULL;

gboolean
guest_account_get_is_enabled ()
{
    return config_get_boolean (config_get_instance (), "GuestAccount", "enabled");
}

const gchar *
guest_account_get_username ()
{
    if (username)
        return username;

    username = config_get_string (config_get_instance (), "GuestAccount", "username");
    if (!username)
        username = g_strdup ("guest");

    return username;
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

gboolean
guest_account_ref ()
{
    gchar *setup_script;
    gchar *stdout_text = NULL;
    gint exit_status;
    gboolean result;
    GError *error = NULL;

    /* If already opened then no action required */
    if (ref_count > 0)
    {
        ref_count++;
        return TRUE;
    }

    if (!guest_account_get_is_enabled ())
        return FALSE;

    setup_script = config_get_string (config_get_instance (), "GuestAccount", "setup-script");
    if (!setup_script)
        return FALSE;

    g_debug ("Opening guest account with script %s", setup_script);

    result = run_script (setup_script, &stdout_text, &exit_status, &error);
    if (!result)
        g_warning ("Error running guest account setup script '%s': %s", setup_script, error->message);
    g_free (setup_script);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    if (exit_status != 0)
    {
        g_warning ("Guest account setup script returns %d: %s", exit_status, stdout_text);
        result = FALSE;
    }
    else
    {
        g_debug ("Guest account setup");
    }

    g_free (stdout_text);

    if (result)
    {
        ref_count++;
        return TRUE;
    }
    else
        return FALSE;
}

void
guest_account_unref ()
{
    gchar *cleanup_script;

    g_return_if_fail (ref_count > 0);

    ref_count--;
    if (ref_count > 0)
        return;

    cleanup_script = config_get_string (config_get_instance (), "GuestAccount", "cleanup-script");
    if (cleanup_script)
    {
        gint exit_status;
        GError *error = NULL;

        g_debug ("Closing guest account with script %s", cleanup_script);

        if (run_script (cleanup_script, NULL, &exit_status, &error))
        {
            if (exit_status != 0)
                g_warning ("Guest account cleanup script returns %d", exit_status);
        }
        else
            g_warning ("Error running guest account cleanup script '%s': %s", cleanup_script, error->message);
        g_clear_error (&error);
    }
    else
        g_debug ("Closing guest account");

    g_free (cleanup_script);
}
