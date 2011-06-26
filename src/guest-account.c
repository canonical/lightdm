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

struct GuestAccountPrivate
{
    gchar *username;
};

G_DEFINE_TYPE (GuestAccount, guest_account, G_TYPE_OBJECT);

static GuestAccount *guest_account_instance = NULL;

GuestAccount *
guest_account_get_instance ()
{
    if (!guest_account_instance)
        guest_account_instance = g_object_new (GUEST_ACCOUNT_TYPE, NULL);
    return guest_account_instance;
}

gboolean
guest_account_get_is_enabled (GuestAccount *account)
{
    return config_get_boolean (config_get_instance (), "GuestAccount", "enabled");
}

const gchar *
guest_account_get_username (GuestAccount *account)
{
    return account->priv->username;
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
guest_account_setup (GuestAccount *account)
{
    gchar *setup_script;
    gchar *stdout_text = NULL;
    gint exit_status;
    gboolean result;
    GError *error = NULL;

    if (!guest_account_get_is_enabled (account))
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
        g_free (account->priv->username);
        account->priv->username = g_strdup (g_strstrip (stdout_text));
        g_debug ("Guest account setup with username '%s'", account->priv->username);
    }

    g_free (stdout_text);

    return result;
}

void
guest_account_cleanup (GuestAccount *account)
{
    gchar *cleanup_script;
    gint exit_status;
    GError *error = NULL;

    if (!guest_account_get_is_enabled (account))
        return;

    cleanup_script = config_get_string (config_get_instance (), "GuestAccount", "cleanup-script");
    if (!cleanup_script)
        return;

    g_debug ("Closing guest account with script %s", cleanup_script);

    if (run_script (cleanup_script, NULL, &exit_status, &error))
    {
        if (exit_status != 0)
            g_warning ("Guest account cleanup script returns %d", exit_status);
    }
    else
        g_warning ("Error running guest account cleanup script '%s': %s", cleanup_script, error->message);
    g_clear_error (&error);

    g_free (cleanup_script);
}

static void
guest_account_init (GuestAccount *account)
{
    account->priv = G_TYPE_INSTANCE_GET_PRIVATE (account, GUEST_ACCOUNT_TYPE, GuestAccountPrivate);  
}

static void
guest_account_finalize (GObject *object)
{
    GuestAccount *self = GUEST_ACCOUNT (object);

    g_free (self->priv->username);

    G_OBJECT_CLASS (guest_account_parent_class)->finalize (object);
}

static void
guest_account_class_init (GuestAccountClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = guest_account_finalize;

    g_type_class_add_private (klass, sizeof (GuestAccountPrivate));
}
