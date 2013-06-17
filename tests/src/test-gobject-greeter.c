/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <lightdm.h>

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

static void
signal_cb (int signum)
{
    status_notify ("%s TERMINATE SIGNAL=%d", greeter_id, signum);
    exit (EXIT_SUCCESS);
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
}

int
main (int argc, char **argv)
{
    gchar *display;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    display = getenv ("DISPLAY");
    if (display == NULL)
        greeter_id = g_strdup ("GREETER-?");
    else if (display[0] == ':')
        greeter_id = g_strdup_printf ("GREETER-X-%s", display + 1);
    else
        greeter_id = g_strdup_printf ("GREETER-X-%s", display);

    loop = g_main_loop_new (NULL, FALSE);

    status_connect (request_cb);

    status_notify ("%s START", greeter_id);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (g_key_file_has_key (config, "test-greeter-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-greeter-config", "return-value", NULL);
        status_notify ("%s EXIT CODE=%d", greeter_id, return_value);
        return return_value;
    }

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        status_notify ("%s FAIL-CONNECT-XSERVER", greeter_id);
        return EXIT_FAILURE;
    }

    status_notify ("%s CONNECT-XSERVER", greeter_id);

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "autologin-timer-expired", G_CALLBACK (autologin_timer_expired_cb), NULL);

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

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
