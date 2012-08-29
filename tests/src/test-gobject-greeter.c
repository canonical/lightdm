/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <lightdm.h>

#include "status.h"

static LightDMGreeter *greeter;
static xcb_connection_t *connection = NULL;
static GKeyFile *config;

static void
show_message_cb (LightDMGreeter *greeter, const gchar *text, LightDMMessageType type)
{
    status_notify ("GREETER %s SHOW-MESSAGE TEXT=\"%s\"", getenv ("DISPLAY"), text);
}

static void
show_prompt_cb (LightDMGreeter *greeter, const gchar *text, LightDMPromptType type)
{
    status_notify ("GREETER %s SHOW-PROMPT TEXT=\"%s\"", getenv ("DISPLAY"), text);
}

static void
authentication_complete_cb (LightDMGreeter *greeter)
{
    if (lightdm_greeter_get_authentication_user (greeter))
        status_notify ("GREETER %s AUTHENTICATION-COMPLETE USERNAME=%s AUTHENTICATED=%s",
                       getenv ("DISPLAY"),
                       lightdm_greeter_get_authentication_user (greeter),
                       lightdm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
    else
        status_notify ("GREETER %s AUTHENTICATION-COMPLETE AUTHENTICATED=%s",
                       getenv ("DISPLAY"),
                       lightdm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
}

static void
signal_cb (int signum)
{
    status_notify ("GREETER %s TERMINATE SIGNAL=%d", getenv ("DISPLAY"), signum);
    exit (EXIT_SUCCESS);
}

static void
request_cb (const gchar *request)
{
    gchar *r;
  
    r = g_strdup_printf ("GREETER %s AUTHENTICATE", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
        lightdm_greeter_authenticate (greeter, NULL);
    g_free (r);

    r = g_strdup_printf ("GREETER %s AUTHENTICATE USERNAME=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
        lightdm_greeter_authenticate (greeter, request + strlen (r));
    g_free (r);

    r = g_strdup_printf ("GREETER %s AUTHENTICATE-GUEST", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
        lightdm_greeter_authenticate_as_guest (greeter);
    g_free (r);

    r = g_strdup_printf ("GREETER %s AUTHENTICATE-REMOTE SESSION=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
        lightdm_greeter_authenticate_remote (greeter, request + strlen (r), NULL);
    g_free (r);

    r = g_strdup_printf ("GREETER %s RESPOND TEXT=\"", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
    {
        gchar *text = g_strdup (request + strlen (r));
        text[strlen (text) - 1] = '\0';
        lightdm_greeter_respond (greeter, text);
        g_free (text);
    }
    g_free (r);

    r = g_strdup_printf ("GREETER %s START-SESSION", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
    {
        if (!lightdm_greeter_start_session_sync (greeter, NULL, NULL))
            status_notify ("GREETER %s SESSION-FAILED", getenv ("DISPLAY")); 
    }
    g_free (r);

    r = g_strdup_printf ("GREETER %s START-SESSION SESSION=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
    {
        if (!lightdm_greeter_start_session_sync (greeter, request + strlen (r), NULL))
            status_notify ("GREETER %s SESSION-FAILED", getenv ("DISPLAY")); 
    }
    g_free (r);

    r = g_strdup_printf ("GREETER %s LOG-LAYOUT", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
    {
        const gchar *layout;
        layout = lightdm_layout_get_name (lightdm_get_layout ());
        status_notify ("GREETER %s LOG-LAYOUT LAYOUT='%s'", getenv ("DISPLAY"), layout ? layout : "");
    }

    r = g_strdup_printf ("GREETER %s LOG-LAYOUT USERNAME=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
    {
        LightDMUser *user;
        const gchar *username, *layout;

        username = request + strlen (r);
        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        layout = lightdm_user_get_layout (user);

        status_notify ("GREETER %s LOG-LAYOUT USERNAME=%s LAYOUT='%s'", getenv ("DISPLAY"), username, layout ? layout : "");
    }
    g_free (r);

    r = g_strdup_printf ("GREETER %s LOG-LAYOUTS USERNAME=", getenv ("DISPLAY"));
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
            status_notify ("GREETER %s LOG-LAYOUTS USERNAME=%s LAYOUT='%s'", getenv ("DISPLAY"), username, layouts[i]);
    }
    g_free (r);

    r = g_strdup_printf ("GREETER %s LOG-VARIANTS LAYOUT=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
    {
        GList *layouts, *iter;
        const gchar *layout_prefix;

        layout_prefix = request + strlen (r);
        layouts = lightdm_get_layouts ();

        for (iter = layouts; iter; iter = iter->next)
        {
            LightDMLayout *layout;
            const gchar *name;

            layout = (LightDMLayout *) iter->data;
            name = lightdm_layout_get_name (layout);

            if (g_str_has_prefix (name, layout_prefix))
                status_notify ("GREETER %s LOG-VARIANTS LAYOUT='%s'", getenv ("DISPLAY"), name);
        }
    }
    g_free (r);

    r = g_strdup_printf ("GREETER %s LOG-LANGUAGE USERNAME=", getenv ("DISPLAY"));  
    if (g_str_has_prefix (request, r))
    {
        LightDMUser *user;
        const gchar *username, *language;

        username = request + strlen (r);
        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        language = lightdm_user_get_language (user);

        status_notify ("GREETER %s LOG-LANGUAGE USERNAME=%s LANGUAGE=%s", getenv ("DISPLAY"), username, language ? language : "");
    }
    g_free (r);
}

int
main (int argc, char **argv)
{
    GMainLoop *main_loop;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    g_type_init ();

    main_loop = g_main_loop_new (NULL, FALSE);

    status_connect (request_cb);

    status_notify ("GREETER %s START", getenv ("DISPLAY"));

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (g_key_file_has_key (config, "test-greeter-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-greeter-config", "return-value", NULL);
        status_notify ("GREETER %s EXIT CODE=%d", getenv ("DISPLAY"), return_value);
        return return_value;
    }

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        status_notify ("GREETER %s FAIL-CONNECT-XSERVER", getenv ("DISPLAY"));
        return EXIT_FAILURE;
    }

    status_notify ("GREETER %s CONNECT-XSERVER", getenv ("DISPLAY"));

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);

    status_notify ("GREETER %s CONNECT-TO-DAEMON", getenv ("DISPLAY"));
    if (!lightdm_greeter_connect_sync (greeter, NULL))
    {
        status_notify ("GREETER %s FAIL-CONNECT-DAEMON", getenv ("DISPLAY"));
        return EXIT_FAILURE;
    }

    status_notify ("GREETER %s CONNECTED-TO-DAEMON", getenv ("DISPLAY"));

    if (lightdm_greeter_get_select_user_hint (greeter))
        status_notify ("GREETER %s SELECT-USER-HINT USERNAME=%s", getenv ("DISPLAY"), lightdm_greeter_get_select_user_hint (greeter));
    if (lightdm_greeter_get_lock_hint (greeter))
        status_notify ("GREETER %s LOCK-HINT", getenv ("DISPLAY"));

    g_main_loop_run (main_loop);

    return EXIT_SUCCESS;
}
