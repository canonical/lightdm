/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <lightdm.h>
#include <glib-unix.h>
#include <ctype.h>

#include "status.h"

static gchar *greeter_id;
static GMainLoop *loop;
static LightDMGreeter *greeter;
static xcb_connection_t *connection = NULL;
static GKeyFile *config;

static void
show_message_cb (LightDMGreeter *greeter, const gchar *text, LightDMMessageType type)
{
    status_notify ("%s SHOW-MESSAGE TEXT=\"%s\"", greeter_id, text);
}

static void
show_prompt_cb (LightDMGreeter *greeter, const gchar *text, LightDMPromptType type)
{
    status_notify ("%s SHOW-PROMPT TEXT=\"%s\"", greeter_id, text);
}

static void
authentication_complete_cb (LightDMGreeter *greeter)
{
    if (lightdm_greeter_get_authentication_user (greeter))
        status_notify ("%s AUTHENTICATION-COMPLETE USERNAME=%s AUTHENTICATED=%s",
                       greeter_id,
                       lightdm_greeter_get_authentication_user (greeter),
                       lightdm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
    else
        status_notify ("%s AUTHENTICATION-COMPLETE AUTHENTICATED=%s",
                       greeter_id,
                       lightdm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
}

static void
autologin_timer_expired_cb (LightDMGreeter *greeter)
{
    status_notify ("%s AUTOLOGIN-TIMER-EXPIRED", greeter_id);
}

static gboolean
sigint_cb (gpointer user_data)
{
    status_notify ("%s TERMINATE SIGNAL=%d", greeter_id, SIGINT);
    g_main_loop_quit (loop);
    return TRUE;
}

static gboolean
sigterm_cb (gpointer user_data)
{
    status_notify ("%s TERMINATE SIGNAL=%d", greeter_id, SIGTERM);
    g_main_loop_quit (loop);
    return TRUE;
}

static void
user_changed_cb (LightDMUser *user)
{
    status_notify ("%s USER-CHANGED USERNAME=%s", greeter_id, lightdm_user_get_name (user));
}

static void
request_cb (const gchar *request)
{
    const gchar *c, *start;
    int l;
    gchar *id, *name = NULL;
    gboolean id_matches;
    GHashTable *params;

    if (!request)
    {
        g_main_loop_quit (loop);
        return;
    }

    c = request;
    start = c;
    l = 0;
    while (*c && !isspace (*c))
    {
        c++;
        l++;
    }
    id = g_strdup_printf ("%.*s", l, start);
    id_matches = strcmp (id, greeter_id) == 0;
    g_free (id);
    if (!id_matches)
        return;

    while (isspace (*c))
        c++;
    start = c;
    l = 0;
    while (*c && !isspace (*c))
    {
        c++;
        l++;
    }
    name = g_strdup_printf ("%.*s", l, start);

    params = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    while (TRUE)
    {
        const gchar *start;
        gchar *param_name, *param_value;

        while (isspace (*c))
            c++;
        start = c;
        while (*c && !isspace (*c) && *c != '=')
            c++;
        if (*c == '\0')
            break;

        param_name = g_strdup_printf ("%.*s", (int) (c - start), start);

        if (*c == '=')
        {
            c++;
            while (isspace (*c))
                c++;
            if (*c == '\"')
            {
                gboolean escaped = FALSE;
                GString *value;

                c++;
                value = g_string_new ("");
                while (*c)
                {
                    if (*c == '\\')
                    {
                        if (escaped)
                        {
                            g_string_append_c (value, '\\');
                            escaped = FALSE;
                        }
                        else
                            escaped = TRUE;
                    }
                    else if (!escaped && *c == '\"')
                        break;
                    if (!escaped)
                        g_string_append_c (value, *c);
                    c++;
                }
                param_value = value->str;
                g_string_free (value, FALSE);
                if (*c == '\"')
                    c++;
            }
            else
            {
                start = c;
                while (*c && !isspace (*c))
                    c++;
                param_value = g_strdup_printf ("%.*s", (int) (c - start), start);
            }
        }
        else
            param_value = g_strdup ("");

        g_hash_table_insert (params, param_name, param_value);
    }
  
    if (strcmp (name, "AUTHENTICATE") == 0)
        lightdm_greeter_authenticate (greeter, g_hash_table_lookup (params, "USERNAME"));

    if (strcmp (name, "AUTHENTICATE-GUEST") == 0)
        lightdm_greeter_authenticate_as_guest (greeter);

    if (strcmp (name, "AUTHENTICATE-AUTOLOGIN") == 0)
        lightdm_greeter_authenticate_autologin (greeter);

    if (strcmp (name, "AUTHENTICATE-REMOTE") == 0)
        lightdm_greeter_authenticate_remote (greeter, g_hash_table_lookup (params, "SESSION"), NULL);

    if (strcmp (name, "RESPOND") == 0)
        lightdm_greeter_respond (greeter, g_hash_table_lookup (params, "TEXT"));

    if (strcmp (name, "CANCEL-AUTHENTICATION") == 0)
        lightdm_greeter_cancel_authentication (greeter);

    if (strcmp (name, "START-SESSION") == 0)
        if (!lightdm_greeter_start_session_sync (greeter, g_hash_table_lookup (params, "SESSION"), NULL))
            status_notify ("%s SESSION-FAILED", greeter_id); 

    if (strcmp (name, "LOG-DEFAULT-SESSION") == 0)
        status_notify ("%s LOG-DEFAULT-SESSION SESSION=%s", greeter_id, lightdm_greeter_get_default_session_hint (greeter));

    if (strcmp (name, "LOG-USER-LIST-LENGTH") == 0)
        status_notify ("%s LOG-USER-LIST-LENGTH N=%d", greeter_id, lightdm_user_list_get_length (lightdm_user_list_get_instance ()));

    if (strcmp (name, "ENSURE-SHARED-DATA-DIR") == 0)
        status_notify ("%s ENSURE-SHARED-DATA-DIR RESULT=%s", greeter_id, lightdm_greeter_ensure_shared_data_dir_sync (greeter, g_hash_table_lookup (params, "USERNAME")) ? "TRUE" : "FALSE");

    if (strcmp (name, "WATCH-USER") == 0)
    {
        LightDMUser *user;
        const gchar *username;

        username = g_hash_table_lookup (params, "USERNAME");
        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        if (user)
            g_signal_connect (user, "changed", G_CALLBACK (user_changed_cb), NULL);
        status_notify ("%s WATCH-USER USERNAME=%s", greeter_id, username);
    }

    if (strcmp (name, "LOG-USER") == 0)
    {
        LightDMUser *user;
        const gchar *username, *image, *background, *language, *layout, *session;
        const gchar * const * layouts;
        gchar **fields = NULL;
        gchar *layouts_text;
        GString *status_text;
        int i;

        username = g_hash_table_lookup (params, "USERNAME");
        if (g_hash_table_lookup (params, "FIELDS"))
            fields = g_strsplit (g_hash_table_lookup (params, "FIELDS"), ",", -1);
        if (!fields)
        {
            fields = g_malloc (sizeof (gchar *) * 1);
            fields[0] = NULL;
        }

        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        image = lightdm_user_get_image (user);
        background = lightdm_user_get_background (user);
        language = lightdm_user_get_language (user);
        layout = lightdm_user_get_layout (user);
        layouts = lightdm_user_get_layouts (user);
        layouts_text = g_strjoinv (";", (gchar **) layouts);
        session = lightdm_user_get_session (user);

        status_text = g_string_new ("");
        g_string_append_printf (status_text, "%s LOG-USER USERNAME=%s", greeter_id, username);
        for (i = 0; fields[i]; i++)
        {
            if (strcmp (fields[i], "REAL-NAME") == 0)
                g_string_append_printf (status_text, " REAL-NAME=%s", lightdm_user_get_real_name (user));
            else if (strcmp (fields[i], "DISPLAY-NAME") == 0)
                g_string_append_printf (status_text, " DISPLAY-NAME=%s", lightdm_user_get_display_name (user));
            else if (strcmp (fields[i], "IMAGE") == 0)
                g_string_append_printf (status_text, " IMAGE=%s", image ? image : "");
            else if (strcmp (fields[i], "BACKGROUND") == 0)
                g_string_append_printf (status_text, " BACKGROUND=%s", background ? background : "");
            else if (strcmp (fields[i], "LANGUAGE") == 0)
                g_string_append_printf (status_text, " LANGUAGE=%s", language ? language : "");
            else if (strcmp (fields[i], "LAYOUT") == 0)
                g_string_append_printf (status_text, " LAYOUT=%s", layout ? layout : "");
            else if (strcmp (fields[i], "LAYOUTS") == 0)
                g_string_append_printf (status_text, " LAYOUTS=%s", layouts_text);
            else if (strcmp (fields[i], "SESSION") == 0)
                g_string_append_printf (status_text, " SESSION=%s", session ? session : "");
            else if (strcmp (fields[i], "LOGGED-IN") == 0)
                g_string_append_printf (status_text, " LOGGED-IN=%s", lightdm_user_get_logged_in (user) ? "TRUE" : "FALSE");
            else if (strcmp (fields[i], "HAS-MESSAGES") == 0)
                g_string_append_printf (status_text, " HAS-MESSAGES=%s", lightdm_user_get_has_messages (user) ? "TRUE" : "FALSE");
        }
        g_strfreev (fields);
        g_free (layouts_text);

        status_notify (status_text->str);
        g_string_free (status_text, TRUE);
    }

    if (strcmp (name, "LOG-USER-LIST") == 0)
    {
        GList *users, *link;

        users = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
        for (link = users; link; link = link->next)
        {
            LightDMUser *user = link->data;
            status_notify ("%s LOG-USER USERNAME=%s", greeter_id, lightdm_user_get_name (user));
        }
    }

    if (strcmp (name, "GET-CAN-SUSPEND") == 0)
    {
        gboolean can_suspend = lightdm_get_can_suspend ();
        status_notify ("%s CAN-SUSPEND ALLOWED=%s", greeter_id, can_suspend ? "TRUE" : "FALSE");
    }

    if (strcmp (name, "SUSPEND") == 0)
    {
        GError *error = NULL;
        if (!lightdm_suspend (&error))
            status_notify ("%s FAIL-SUSPEND", greeter_id);
        g_clear_error (&error);
    }

    if (strcmp (name, "GET-CAN-HIBERNATE") == 0)
    {
        gboolean can_hibernate = lightdm_get_can_hibernate ();
        status_notify ("%s CAN-HIBERNATE ALLOWED=%s", greeter_id, can_hibernate ? "TRUE" : "FALSE");
    }

    if (strcmp (name, "HIBERNATE") == 0)
    {
        GError *error = NULL;
        if (!lightdm_hibernate (&error))
            status_notify ("%s FAIL-HIBERNATE", greeter_id);
        g_clear_error (&error);
    }

    if (strcmp (name, "GET-CAN-RESTART") == 0)
    {
        gboolean can_restart = lightdm_get_can_restart ();
        status_notify ("%s CAN-RESTART ALLOWED=%s", greeter_id, can_restart ? "TRUE" : "FALSE");
    }

    if (strcmp (name, "RESTART") == 0)
    {
        GError *error = NULL;
        if (!lightdm_restart (&error))
            status_notify ("%s FAIL-RESTART", greeter_id);
        g_clear_error (&error);
    }

    if (strcmp (name, "GET-CAN-SHUTDOWN") == 0)
    {
        gboolean can_shutdown = lightdm_get_can_shutdown ();
        status_notify ("%s CAN-SHUTDOWN ALLOWED=%s", greeter_id, can_shutdown ? "TRUE" : "FALSE");
    }

    if (strcmp (name, "SHUTDOWN") == 0)
    {
        GError *error = NULL;
        if (!lightdm_shutdown (&error))
            status_notify ("%s FAIL-SHUTDOWN", greeter_id);
        g_clear_error (&error);
    }

    g_free (name);
    g_hash_table_unref (params);
}

static void
user_added_cb (LightDMUserList *user_list, LightDMUser *user)
{
    status_notify ("%s USER-ADDED USERNAME=%s", greeter_id, lightdm_user_get_name (user));
}

static void
user_removed_cb (LightDMUserList *user_list, LightDMUser *user)
{
    status_notify ("%s USER-REMOVED USERNAME=%s", greeter_id, lightdm_user_get_name (user));
}

int
main (int argc, char **argv)
{
    gchar *display, *xdg_seat, *xdg_vtnr, *xdg_session_cookie, *xdg_session_class, *mir_socket, *mir_vt, *mir_id;
    GString *status_text;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    display = getenv ("DISPLAY");
    xdg_seat = getenv ("XDG_SEAT");
    xdg_vtnr = getenv ("XDG_VTNR");
    xdg_session_cookie = getenv ("XDG_SESSION_COOKIE");
    xdg_session_class = getenv ("XDG_SESSION_CLASS");
    mir_socket = getenv ("MIR_SOCKET");
    mir_vt = getenv ("MIR_SERVER_VT");
    mir_id = getenv ("MIR_SERVER_NAME");
    if (display)
    {
        if (display[0] == ':')
            greeter_id = g_strdup_printf ("GREETER-X-%s", display + 1);
        else
            greeter_id = g_strdup_printf ("GREETER-X-%s", display);
    }
    else if (mir_id)
        greeter_id = g_strdup_printf ("GREETER-MIR-%s", mir_id);
    else if (mir_socket || mir_vt)
        greeter_id = g_strdup ("GREETER-MIR");
    else
        greeter_id = g_strdup ("GREETER-?");

    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGINT, sigint_cb, NULL);
    g_unix_signal_add (SIGTERM, sigterm_cb, NULL);

    status_connect (request_cb);

    status_text = g_string_new ("");
    g_string_printf (status_text, "%s START", greeter_id);
    if (xdg_seat)
        g_string_append_printf (status_text, " XDG_SEAT=%s", xdg_seat);
    if (xdg_vtnr)
        g_string_append_printf (status_text, " XDG_VTNR=%s", xdg_vtnr);
    if (xdg_session_cookie)
        g_string_append_printf (status_text, " XDG_SESSION_COOKIE=%s", xdg_session_cookie);
    if (xdg_session_class)
        g_string_append_printf (status_text, " XDG_SESSION_CLASS=%s", xdg_session_class);
    if (mir_vt > 0)
        g_string_append_printf (status_text, " MIR_SERVER_VT=%s", mir_vt);
    status_notify (status_text->str);
    g_string_free (status_text, TRUE);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (g_key_file_has_key (config, "test-greeter-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-greeter-config", "return-value", NULL);
        status_notify ("%s EXIT CODE=%d", greeter_id, return_value);
        return return_value;
    }

    if (display)
    {
        connection = xcb_connect (NULL, NULL);
        if (xcb_connection_has_error (connection))
        {
            status_notify ("%s FAIL-CONNECT-XSERVER", greeter_id);
            return EXIT_FAILURE;
        }
        status_notify ("%s CONNECT-XSERVER", greeter_id);
    }

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "autologin-timer-expired", G_CALLBACK (autologin_timer_expired_cb), NULL);

    if (g_key_file_get_boolean (config, "test-greeter-config", "log-user-changes", NULL))
    {
        g_signal_connect (lightdm_user_list_get_instance (), "user-added", G_CALLBACK (user_added_cb), NULL);
        g_signal_connect (lightdm_user_list_get_instance (), "user-removed", G_CALLBACK (user_removed_cb), NULL);
    }

    status_notify ("%s CONNECT-TO-DAEMON", greeter_id);
    if (!lightdm_greeter_connect_sync (greeter, NULL))
    {
        status_notify ("%s FAIL-CONNECT-DAEMON", greeter_id);
        return EXIT_FAILURE;
    }

    status_notify ("%s CONNECTED-TO-DAEMON", greeter_id);

    if (lightdm_greeter_get_select_user_hint (greeter))
        status_notify ("%s SELECT-USER-HINT USERNAME=%s", greeter_id, lightdm_greeter_get_select_user_hint (greeter));
    if (lightdm_greeter_get_select_guest_hint (greeter))
        status_notify ("%s SELECT-GUEST-HINT", greeter_id);
    if (lightdm_greeter_get_lock_hint (greeter))
        status_notify ("%s LOCK-HINT", greeter_id);
    if (!lightdm_greeter_get_has_guest_account_hint (greeter))
        status_notify ("%s HAS-GUEST-ACCOUNT-HINT=FALSE", greeter_id);
    if (lightdm_greeter_get_hide_users_hint (greeter))
        status_notify ("%s HIDE-USERS-HINT", greeter_id);
    if (lightdm_greeter_get_show_manual_login_hint (greeter))
        status_notify ("%s SHOW-MANUAL-LOGIN-HINT", greeter_id);
    if (!lightdm_greeter_get_show_remote_login_hint (greeter))
        status_notify ("%s SHOW-REMOTE-LOGIN-HINT=FALSE", greeter_id);

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
