/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <lightdm.h>
#include <glib-unix.h>

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
request_cb (const gchar *request)
{
    gchar *r;

    if (!request)
    {
        g_main_loop_quit (loop);
        return;
    }
  
    r = g_strdup_printf ("%s AUTHENTICATE", greeter_id);
    if (strcmp (request, r) == 0)
        lightdm_greeter_authenticate (greeter, NULL);
    g_free (r);

    r = g_strdup_printf ("%s AUTHENTICATE USERNAME=", greeter_id);
    if (g_str_has_prefix (request, r))
        lightdm_greeter_authenticate (greeter, request + strlen (r));
    g_free (r);

    r = g_strdup_printf ("%s AUTHENTICATE-GUEST", greeter_id);
    if (strcmp (request, r) == 0)
        lightdm_greeter_authenticate_as_guest (greeter);
    g_free (r);

    r = g_strdup_printf ("%s AUTHENTICATE-AUTOLOGIN", greeter_id);
    if (strcmp (request, r) == 0)
        lightdm_greeter_authenticate_autologin (greeter);
    g_free (r);

    r = g_strdup_printf ("%s AUTHENTICATE-REMOTE SESSION=", greeter_id);
    if (g_str_has_prefix (request, r))
        lightdm_greeter_authenticate_remote (greeter, request + strlen (r), NULL);
    g_free (r);

    r = g_strdup_printf ("%s RESPOND TEXT=\"", greeter_id);
    if (g_str_has_prefix (request, r))
    {
        gchar *text = g_strdup (request + strlen (r));
        text[strlen (text) - 1] = '\0';
        lightdm_greeter_respond (greeter, text);
        g_free (text);
    }
    g_free (r);

    r = g_strdup_printf ("%s CANCEL-AUTHENTICATION", greeter_id);
    if (strcmp (request, r) == 0)
        lightdm_greeter_cancel_authentication (greeter);
    g_free (r);

    r = g_strdup_printf ("%s START-SESSION", greeter_id);
    if (strcmp (request, r) == 0)
    {
        if (!lightdm_greeter_start_session_sync (greeter, NULL, NULL))
            status_notify ("%s SESSION-FAILED", greeter_id); 
    }
    g_free (r);

    r = g_strdup_printf ("%s START-SESSION SESSION=", greeter_id);
    if (g_str_has_prefix (request, r))
    {
        if (!lightdm_greeter_start_session_sync (greeter, request + strlen (r), NULL))
            status_notify ("%s SESSION-FAILED", greeter_id); 
    }
    g_free (r);

    r = g_strdup_printf ("%s LOG-DEFAULT-SESSION", greeter_id);
    if (strcmp (request, r) == 0)
        status_notify ("%s LOG-DEFAULT-SESSION SESSION=%s", greeter_id, lightdm_greeter_get_default_session_hint (greeter));
    g_free (r);

    r = g_strdup_printf ("%s LOG-USER-LIST-LENGTH", greeter_id);
    if (strcmp (request, r) == 0)
        status_notify ("%s LOG-USER-LIST-LENGTH N=%d", greeter_id, lightdm_user_list_get_length (lightdm_user_list_get_instance ()));
    g_free (r);

    r = g_strdup_printf ("%s LOG-USER USERNAME=", greeter_id);
    if (g_str_has_prefix (request, r))
    {
        LightDMUser *user;
        const gchar *username;

        username = request + strlen (r);
        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        status_notify ("%s LOG-USER USERNAME=%s", greeter_id, lightdm_user_get_name (user));
    }
    g_free (r);

    r = g_strdup_printf ("%s LOG-USER-LIST", greeter_id);
    if (strcmp (request, r) == 0)
    {
        GList *users, *link;

        users = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
        for (link = users; link; link = link->next)
        {
            LightDMUser *user = link->data;
            status_notify ("%s LOG-USER USERNAME=%s", greeter_id, lightdm_user_get_name (user));
        }
    }
    g_free (r);

    r = g_strdup_printf ("%s LOG-LAYOUT USERNAME=", greeter_id);
    if (g_str_has_prefix (request, r))
    {
        LightDMUser *user;
        const gchar *username, *layout;

        username = request + strlen (r);
        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        layout = lightdm_user_get_layout (user);

        status_notify ("%s LOG-LAYOUT USERNAME=%s LAYOUT='%s'", greeter_id, username, layout ? layout : "");
    }
    g_free (r);

    r = g_strdup_printf ("%s LOG-LAYOUTS USERNAME=", greeter_id);
    if (g_str_has_prefix (request, r))
    {
        LightDMUser *user;
        const gchar *username;
        const gchar * const *layouts;
        int i;

        username = request + strlen (r);
        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        layouts = lightdm_user_get_layouts (user);

        for (i = 0; layouts[i]; i++)
            status_notify ("%s LOG-LAYOUTS USERNAME=%s LAYOUT='%s'", greeter_id, username, layouts[i]);
    }
    g_free (r);

    r = g_strdup_printf ("%s LOG-LANGUAGE USERNAME=", greeter_id);  
    if (g_str_has_prefix (request, r))
    {
        LightDMUser *user;
        const gchar *username, *language;

        username = request + strlen (r);
        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        language = lightdm_user_get_language (user);

        status_notify ("%s LOG-LANGUAGE USERNAME=%s LANGUAGE=%s", greeter_id, username, language ? language : "");
    }
    g_free (r);

    r = g_strdup_printf ("%s GET-CAN-SUSPEND", greeter_id);
    if (strcmp (request, r) == 0)
    {
        gboolean can_suspend = lightdm_get_can_suspend ();
        status_notify ("%s CAN-SUSPEND ALLOWED=%s", greeter_id, can_suspend ? "TRUE" : "FALSE");
    }
    g_free (r);

    r = g_strdup_printf ("%s SUSPEND", greeter_id);
    if (strcmp (request, r) == 0)
    {
        GError *error = NULL;
        if (!lightdm_suspend (&error))
            status_notify ("%s FAIL-SUSPEND", greeter_id);
        g_clear_error (&error);
    }
    g_free (r);

    r = g_strdup_printf ("%s GET-CAN-HIBERNATE", greeter_id);
    if (strcmp (request, r) == 0)
    {
        gboolean can_hibernate = lightdm_get_can_hibernate ();
        status_notify ("%s CAN-HIBERNATE ALLOWED=%s", greeter_id, can_hibernate ? "TRUE" : "FALSE");
    }
    g_free (r);

    r = g_strdup_printf ("%s HIBERNATE", greeter_id);
    if (strcmp (request, r) == 0)
    {
        GError *error = NULL;
        if (!lightdm_hibernate (&error))
            status_notify ("%s FAIL-HIBERNATE", greeter_id);
        g_clear_error (&error);
    }
    g_free (r);

    r = g_strdup_printf ("%s GET-CAN-RESTART", greeter_id);
    if (strcmp (request, r) == 0)
    {
        gboolean can_restart = lightdm_get_can_restart ();
        status_notify ("%s CAN-RESTART ALLOWED=%s", greeter_id, can_restart ? "TRUE" : "FALSE");
    }
    g_free (r);

    r = g_strdup_printf ("%s RESTART", greeter_id);
    if (strcmp (request, r) == 0)
    {
        GError *error = NULL;
        if (!lightdm_restart (&error))
            status_notify ("%s FAIL-RESTART", greeter_id);
        g_clear_error (&error);
    }
    g_free (r);

    r = g_strdup_printf ("%s GET-CAN-SHUTDOWN", greeter_id);
    if (strcmp (request, r) == 0)
    {
        gboolean can_shutdown = lightdm_get_can_shutdown ();
        status_notify ("%s CAN-SHUTDOWN ALLOWED=%s", greeter_id, can_shutdown ? "TRUE" : "FALSE");
    }
    g_free (r);

    r = g_strdup_printf ("%s SHUTDOWN", greeter_id);
    if (strcmp (request, r) == 0)
    {
        GError *error = NULL;
        if (!lightdm_shutdown (&error))
            status_notify ("%s FAIL-SHUTDOWN", greeter_id);
        g_clear_error (&error);
    }
    g_free (r);
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
    gchar *display, *xdg_seat, *xdg_vtnr, *xdg_session_cookie, *mir_socket, *mir_vt, *mir_id;
    GString *status_text;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    display = getenv ("DISPLAY");
    xdg_seat = getenv ("XDG_SEAT");
    xdg_vtnr = getenv ("XDG_VTNR");
    xdg_session_cookie = getenv ("XDG_SESSION_COOKIE");
    mir_socket = getenv ("MIR_SERVER_FILE");
    mir_vt = getenv ("MIR_SERVER_VT");
    mir_id = getenv ("MIR_ID");
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
