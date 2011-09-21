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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <grp.h>

#include "session.h"
#include "console-kit.h"

struct SessionPrivate
{
    /* Authentication for this session */
    PAMSession *authentication;

    /* Command to run for this session */
    gchar *command;

    /* ConsoleKit parameters for this session */
    GHashTable *console_kit_parameters;

    /* ConsoleKit cookie for the session */
    gchar *console_kit_cookie;

    /* TRUE if this is a greeter session */
    gboolean is_greeter;
};

G_DEFINE_TYPE (Session, session, PROCESS_TYPE);

void
session_set_authentication (Session *session, PAMSession *authentication)
{
    g_return_if_fail (session != NULL);
    session->priv->authentication = g_object_ref (authentication);
}

PAMSession *
session_get_authentication (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->authentication;
}

User *
session_get_user (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return pam_session_get_user (session->priv->authentication);
}

void
session_set_is_greeter (Session *session, gboolean is_greeter)
{
    g_return_if_fail (session != NULL);
    session->priv->is_greeter = is_greeter;
}

gboolean
session_get_is_greeter (Session *session)
{
    g_return_val_if_fail (session != NULL, FALSE);
    return session->priv->is_greeter;
}

void
session_set_command (Session *session, const gchar *command)
{
    g_return_if_fail (session != NULL);

    g_free (session->priv->command);
    session->priv->command = g_strdup (command);
}

const gchar *
session_get_command (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->command;
}

static gchar *
get_absolute_command (const gchar *command)
{
    gchar **tokens;
    gchar *absolute_binary, *absolute_command = NULL;

    tokens = g_strsplit (command, " ", 2);

    absolute_binary = g_find_program_in_path (tokens[0]);
    if (absolute_binary)
    {
        if (tokens[1])
            absolute_command = g_strjoin (" ", absolute_binary, tokens[1], NULL);
        else
            absolute_command = g_strdup (absolute_binary);
    }

    g_strfreev (tokens);

    return absolute_command;
}

static void
set_env_from_authentication (Session *session, PAMSession *authentication)
{
    gchar **pam_env;

    pam_env = pam_session_get_envlist (authentication);
    if (pam_env)
    {
        gchar *env_string;      
        int i;

        env_string = g_strjoinv (" ", pam_env);
        g_debug ("PAM returns environment '%s'", env_string);
        g_free (env_string);

        for (i = 0; pam_env[i]; i++)
        {
            gchar **pam_env_vars = g_strsplit (pam_env[i], "=", 2);
            if (pam_env_vars && pam_env_vars[0] && pam_env_vars[1])
                session_set_env (session, pam_env_vars[0], pam_env_vars[1]);
            else
                g_warning ("Can't parse PAM environment variable %s", pam_env[i]);
            g_strfreev (pam_env_vars);
        }
        g_strfreev (pam_env);
    }
}

void
session_set_env (Session *session, const gchar *name, const gchar *value)
{
    g_return_if_fail (session != NULL);
    g_return_if_fail (name != NULL);
    process_set_env (PROCESS (session), name, value);
}

const gchar *
session_get_env (Session *session, const gchar *name)
{
    g_return_val_if_fail (session != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    return process_get_env (PROCESS (session), name);
}

void
session_set_console_kit_parameter (Session *session, const gchar *name, GVariant *value)
{
    g_return_if_fail (session != NULL);
    g_return_if_fail (name != NULL);

    g_hash_table_insert (session->priv->console_kit_parameters, g_strdup (name), value);
}

const gchar *
session_get_console_kit_cookie (Session *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->console_kit_cookie;
}

/* Set the LANG variable based on the chosen language.  This is not a great
 * solution, as it will override the language set in PAM (which is where it
 * should be set).  It's also overly simplistic to set all the locale
 * settings based on one language.  In the case of Ubuntu these will be
 * overridden by setting these variables in ~/.profile */
static void
set_language (Session *session)
{
    User *user;
    const gchar *language;
    gchar *language_dot;
    gboolean result;
    gchar *stdout_text = NULL;
    int exit_status;
    gboolean found_code = FALSE;
    GError *error = NULL;

    user = pam_session_get_user (session->priv->authentication);
    language = user_get_language (user);
    if (!language)
        return;
  
    language_dot = g_strdup_printf ("%s.", language);

    /* Find a locale that matches the language code */
    result = g_spawn_command_line_sync ("locale -a", &stdout_text, NULL, &exit_status, &error);
    if (error)
    {
        g_warning ("Failed to run 'locale -a': %s", error->message);
        g_clear_error (&error);
    }
    else if (exit_status != 0)
        g_warning ("Failed to get languages, locale -a returned %d", exit_status);
    else if (result)
    {
        gchar **tokens;
        int i;

        tokens = g_strsplit_set (stdout_text, "\n\r", -1);
        for (i = 0; tokens[i]; i++)
        {
            gchar *code;

            code = g_strchug (tokens[i]);
            if (code[0] == '\0')
                continue;

            if (strcmp (code, language) == 0 || g_str_has_prefix (code, language_dot))
            {
                g_debug ("Using locale %s for language %s", code, language);
                found_code = TRUE;
                session_set_env (session, "LANG", code);
                break;
            }
        }

        g_strfreev (tokens);
    }
    g_free (language_dot);
    g_free (stdout_text);
  
    if (!found_code)
        g_debug ("Failed to find locale for language %s", language);
}

gboolean
session_start (Session *session)
{
    User *user;

    g_return_val_if_fail (session != NULL, FALSE);
    g_return_val_if_fail (session->priv->authentication != NULL, FALSE);
    g_return_val_if_fail (session->priv->command != NULL, FALSE);

    g_debug ("Launching session");

    user = pam_session_get_user (session->priv->authentication);
    session_set_env (session, "PATH", "/usr/local/bin:/usr/bin:/bin");
    session_set_env (session, "USER", user_get_name (user));
    session_set_env (session, "USERNAME", user_get_name (user)); // FIXME: Is this required?
    session_set_env (session, "HOME", user_get_home_directory (user));
    session_set_env (session, "SHELL", user_get_shell (user));

    return SESSION_GET_CLASS (session)->start (session);
}

static gboolean
session_real_start (Session *session)
{
    User *user;
    gboolean result;
    gchar *absolute_command;
    const gchar *orig_path;

    absolute_command = get_absolute_command (session->priv->command);
    if (!absolute_command)
    {
        g_debug ("Can't launch session %s, not found in path", session->priv->command);
        return FALSE;
    }
    process_set_command (PROCESS (session), absolute_command);
    g_free (absolute_command);

    /* Insert our own utility directory to PATH
     * This is to provide gdmflexiserver which provides backwards compatibility with GDM.
     * Must be done after set_env_from_authentication because that often sets PATH.
     * This can be removed when this is no longer required.
     */
    orig_path = session_get_env (session, "PATH");
    if (orig_path)
    {
        gchar *path = g_strdup_printf ("%s:%s", PKGLIBEXEC_DIR, orig_path);
        session_set_env (session, "PATH", path);
        g_free (path);
    }

    pam_session_open (session->priv->authentication);
    set_env_from_authentication (session, session->priv->authentication);

    set_language (session);

    /* Open ConsoleKit session */
    if (getuid () == 0)
    {
        GVariantBuilder parameters;
        User *user;
        GHashTableIter iter;
        gpointer key, value;

        user = pam_session_get_user (session->priv->authentication);

        g_variant_builder_init (&parameters, G_VARIANT_TYPE ("(a(sv))"));
        g_variant_builder_open (&parameters, G_VARIANT_TYPE ("a(sv)"));
        g_variant_builder_add (&parameters, "(sv)", "unix-user", g_variant_new_int32 (user_get_uid (user)));
        if (session->priv->is_greeter)
            g_variant_builder_add (&parameters, "(sv)", "session-type", g_variant_new_string ("LoginWindow"));
        g_hash_table_iter_init (&iter, session->priv->console_kit_parameters);
        while (g_hash_table_iter_next (&iter, &key, &value))
            g_variant_builder_add (&parameters, "(sv)", (gchar *) key, (GVariant *) value);

        g_free (session->priv->console_kit_cookie);
        session->priv->console_kit_cookie = ck_open_session (&parameters);
    }
    else
    {
        g_free (session->priv->console_kit_cookie);
        session->priv->console_kit_cookie = g_strdup (g_getenv ("XDG_SESSION_COOKIE"));
    }

    if (session->priv->console_kit_cookie)
        session_set_env (session, "XDG_SESSION_COOKIE", session->priv->console_kit_cookie);

    user = pam_session_get_user (session->priv->authentication);
    process_set_user (PROCESS (session), user);
    process_set_working_directory (PROCESS (session), user_get_home_directory (user));
    result = process_start (PROCESS (session));
  
    if (!result)
    {
        pam_session_close (session->priv->authentication);
        if (getuid () == 0 && session->priv->console_kit_cookie)
            ck_close_session (session->priv->console_kit_cookie);
    }  

    return result;
}

void
session_unlock (Session *session)
{    
    g_return_if_fail (session != NULL);
    if (getuid () == 0)
        ck_unlock_session (session->priv->console_kit_cookie);
}

void
session_stop (Session *session)
{
    g_return_if_fail (session != NULL);
    SESSION_GET_CLASS (session)->stop (session);
}

static void
session_real_stop (Session *session)
{
    process_signal (PROCESS (session), SIGTERM);
}

static void
session_run (Process *process)
{
    int fd;

    /* Make input non-blocking */
    fd = g_open ("/dev/null", O_RDONLY);
    dup2 (fd, STDIN_FILENO);
    close (fd);

    PROCESS_CLASS (session_parent_class)->run (process);
}

static void
session_stopped (Process *process)
{
    Session *session = SESSION (process);

    pam_session_close (session->priv->authentication);
    if (getuid () == 0 && session->priv->console_kit_cookie)
        ck_close_session (session->priv->console_kit_cookie);

    PROCESS_CLASS (session_parent_class)->stopped (process);
}

static void
session_init (Session *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, SESSION_TYPE, SessionPrivate);
    session->priv->console_kit_parameters = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
}

static void
session_finalize (GObject *object)
{
    Session *self;

    self = SESSION (object);

    if (self->priv->authentication)
        g_object_unref (self->priv->authentication);
    g_free (self->priv->command);
    g_hash_table_unref (self->priv->console_kit_parameters);
    g_free (self->priv->console_kit_cookie);

    G_OBJECT_CLASS (session_parent_class)->finalize (object);
}

static void
session_class_init (SessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    ProcessClass *process_class = PROCESS_CLASS (klass);

    klass->start = session_real_start;
    klass->stop = session_real_stop;
    process_class->run = session_run;
    process_class->stopped = session_stopped;
    object_class->finalize = session_finalize;

    g_type_class_add_private (klass, sizeof (SessionPrivate));
}
