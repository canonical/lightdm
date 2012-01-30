/*
 * Copyright (C) 2011 Didier Roche.
 * Author: Didier Roche <didrocks@ubuntu.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>

#define SEATDEFAULT_KEY_GROUP "SeatDefaults"
#define SESSION_KEY_NAME  "user-session"
#define GREETER_KEY_NAME  "greeter-session"
#define AUTOLOGIN_KEY_NAME  "autologin-user"

#define IS_STRING_EMPTY(x) ((x)==NULL||(x)[0]=='\0')

static gboolean debug = FALSE;
static gboolean keep_old = FALSE;
static gboolean remove = FALSE;

static char    *session = NULL;
static char    *greeter = NULL;
static char    *autologin = NULL;

static GOptionEntry entries[] =
{
  { "debug",    'd', 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging"), NULL },
  { "keep-old", 'k', 0, G_OPTION_ARG_NONE, &keep_old, N_("Only update if no default already set"), NULL },
  { "remove",   'r', 0, G_OPTION_ARG_NONE, &remove, N_("Remove default value if it's the current one"), NULL },
  { "session",  's', 0, G_OPTION_ARG_STRING, &session, N_("Set default session"), NULL },
  { "greeter",  'g', 0, G_OPTION_ARG_STRING, &greeter, N_("Set default greeter"), NULL },
  { "autologin",'a', 0, G_OPTION_ARG_STRING, &autologin, N_("Set autologin user"), NULL },
  { NULL }
};

void
show_nothing(const gchar   *log_domain,
             GLogLevelFlags log_level,
             const gchar   *message,
             gpointer       unused_data) {};

int
update_string(const gchar *default_value,
              const gchar *new_value,
              gboolean     keep_old,
              gboolean     remove,
              const gchar *key_group,
              const gchar *key_name,
              GKeyFile    *keyfile)
{
    gboolean success = TRUE;
        
    if (!(default_value) || (strlen(default_value) < 1)) {
        g_debug ("No existing valid value for %s. Set to %s", key_name, new_value);
        g_key_file_set_string (keyfile, key_group, key_name, new_value);
    }
    else {
        if (remove) {
            if (g_strcmp0 (default_value, new_value) == 0) {
                g_debug ("Remove %s as default value for %s", default_value, key_name);
                g_key_file_set_string (keyfile, key_group, key_name, "");
                if (!success)
                    return(2);
                return(0);
            }
            g_debug ("Can't remove: %s is not the default value for %s", default_value, key_name);
            return(4);
        }
        else {
            g_debug ("Found existing default value(%s) for %s", default_value, key_name);
            if (keep_old)
                g_debug ("keep-old mode: keep previous default value");
            else {
                g_debug ("Update to %s for %s", default_value, key_name);
                g_key_file_set_string (keyfile, key_group, key_name, new_value);
            }
        }
    }
    if (!success)
        return(2);
    return(0);
}

int 
main (int argc, char *argv[])
{
    GOptionContext *context = NULL;
    GError         *error = NULL;

    GKeyFile       *keyfile;
    GKeyFileFlags   flags;
    gchar          *s_data;
    gsize           size;
    const gchar    *gdm_conf_file = CONFIG_DIR "/lightdm.conf";

    gchar          *default_session = NULL;
    gchar          *default_greeter = NULL;
    gchar          *default_autologin = NULL;
    gint            return_code = 0;

    bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    g_type_init ();

    context = g_option_context_new (N_("- set lightdm default values"));
    g_option_context_add_main_entries (context, entries, NULL);
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_printerr (N_("option parsing failed: %s\n"), error->message);
        g_option_context_free (context);
        g_error_free (error);
        return 1;
    }
    if (IS_STRING_EMPTY (session) && IS_STRING_EMPTY (greeter) && IS_STRING_EMPTY (autologin)) {
        g_printerr (N_("Wrong usage of the command\n%s"), g_option_context_get_help (context, FALSE, NULL));
        g_option_context_free (context);
        return 1;
    }
    if (context)
        g_option_context_free (context); 
    if (!debug)
        g_log_set_handler (NULL, G_LOG_LEVEL_DEBUG, show_nothing, NULL);

    keyfile = g_key_file_new ();
    flags = G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;
    if (!(g_key_file_load_from_file (keyfile, gdm_conf_file, flags, &error))) {
            g_debug ("File doesn't seem to exist or can't be read: create one (%s)", error->message);
            g_error_free (error);
            error = NULL;
    }

    // try to get the right keys
    default_session = g_key_file_get_string (keyfile, SEATDEFAULT_KEY_GROUP, SESSION_KEY_NAME, NULL);
    default_greeter = g_key_file_get_string (keyfile, SEATDEFAULT_KEY_GROUP, GREETER_KEY_NAME, NULL);
    default_autologin = g_key_file_get_string (keyfile, SEATDEFAULT_KEY_GROUP, AUTOLOGIN_KEY_NAME, NULL);

    if (!(IS_STRING_EMPTY (session)))
        return_code = update_string (default_session, session, keep_old, remove, SEATDEFAULT_KEY_GROUP, SESSION_KEY_NAME, keyfile);
    if (!(IS_STRING_EMPTY (greeter)) && (return_code == 0))
        return_code = update_string (default_greeter, greeter, keep_old, remove, SEATDEFAULT_KEY_GROUP, GREETER_KEY_NAME, keyfile);
    if (!(IS_STRING_EMPTY (autologin)) && (return_code == 0))
        return_code = update_string (default_autologin, autologin, keep_old, remove, SEATDEFAULT_KEY_GROUP, AUTOLOGIN_KEY_NAME, keyfile);

    if(return_code == 0) {
        s_data = g_key_file_to_data (keyfile, &size, &error);
        if (!s_data) {
            g_debug ("Can't convert data to string: %s", error->message);
            g_error_free (error);
            return_code = 1;
        }
        else {
            if(!g_file_set_contents (gdm_conf_file, s_data, size, &error)) {
                g_printerr ("Can't update: %s\n", error->message);
                g_error_free (error);
                return_code = 1;
            }
            g_free (s_data);
         }
    }

    g_key_file_free (keyfile);

    if (default_session)
        g_free (default_session);
    if (default_greeter)
        g_free (default_greeter);
    if (default_autologin)
        g_free (default_autologin);

    return return_code;

}
