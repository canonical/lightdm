/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <lightdm.h>

#include "status.h"

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
    gchar *username, *password, *response = NULL;

    status_notify ("GREETER %s SHOW-PROMPT TEXT=\"%s\"", getenv ("DISPLAY"), text);

    username = g_key_file_get_string (config, "test-greeter-config", "username", NULL);
    password = g_key_file_get_string (config, "test-greeter-config", "password", NULL);

    if (g_key_file_get_boolean (config, "test-greeter-config", "prompt-username", NULL) && strcmp (text, "login:") == 0)
        response = username;
    else if (password)
        response = password;

    if (response)
    {
        status_notify ("GREETER %s RESPOND TEXT=\"%s\"", getenv ("DISPLAY"), response);
        lightdm_greeter_respond (greeter, response);
    }

    g_free (username);
    g_free (password);
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
    if (!lightdm_greeter_get_is_authenticated (greeter))
        return;

    if (!lightdm_greeter_start_session_sync (greeter, g_key_file_get_string (config, "test-greeter-config", "session", NULL), NULL))
        status_notify ("GREETER %s SESSION-FAILED", getenv ("DISPLAY"));
}

static void
signal_cb (int signum)
{
    status_notify ("GREETER %s TERMINATE SIGNAL=%d", getenv ("DISPLAY"), signum);
    exit (EXIT_SUCCESS);
}

static void
request_cb (const gchar *message)
{
}

int
main (int argc, char **argv)
{
    GMainLoop *main_loop;
    LightDMGreeter *greeter;
    gchar *layout_username, *language_username;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    g_type_init ();

    main_loop = g_main_loop_new (NULL, FALSE);

    status_connect (request_cb);

    status_notify ("GREETER %s START", getenv ("DISPLAY"));

    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

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

    layout_username = g_key_file_get_string (config, "test-greeter-config", "log-keyboard-layout", NULL);
    if (layout_username)
    {
        LightDMUser *user;
        const gchar *layout;

        if (g_strcmp0 (layout_username, "%DEFAULT%") == 0) /* Grab system default layout */
            layout = lightdm_layout_get_name (lightdm_get_layout ());
        else
        {
            user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), layout_username);
            layout = lightdm_user_get_layout (user);
        }

        status_notify ("GREETER %s GET-LAYOUT USERNAME=%s LAYOUT='%s'", getenv ("DISPLAY"), layout_username, layout ? layout : "");
    }

    language_username = g_key_file_get_string (config, "test-greeter-config", "log-language", NULL);
    if (language_username)
    {
        LightDMUser *user;
        const gchar *language;

        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), language_username);
        language = lightdm_user_get_language (user);

        status_notify ("GREETER %s GET-LANGUAGE USERNAME=%s LANGUAGE=%s", getenv ("DISPLAY"), language_username, language ? language : "");
    }

    if (g_key_file_get_boolean (config, "test-greeter-config", "crash-xserver", NULL))
    {
        gchar *crash_lock;
        FILE *f;

        crash_lock = g_build_filename (g_getenv ("LIGHTDM_TEST_HOME_DIR"), ".greeter-crashed-xserver", NULL);
        f = fopen (crash_lock, "r");

        if (f == NULL)
        {
            const gchar *name = "SIGSEGV";
            status_notify ("GREETER %s CRASH-XSERVER", getenv ("DISPLAY"));
            xcb_intern_atom (connection, FALSE, strlen (name), name);
            xcb_flush (connection);

            /* Write lock to stop repeatedly logging in */
            f = fopen (crash_lock, "w");
            fclose (f);
        }
    }

    /* Automatically log in as requested user */
    if (lightdm_greeter_get_select_user_hint (greeter))
    {
        status_notify ("GREETER %s AUTHENTICATE-SELECTED USERNAME=%s", getenv ("DISPLAY"), lightdm_greeter_get_select_user_hint (greeter));
        lightdm_greeter_authenticate (greeter, lightdm_greeter_get_select_user_hint (greeter));
    }
    else
    {
        gchar *login_lock;
        FILE *f;

        login_lock = g_build_filename (g_getenv ("LIGHTDM_TEST_HOME_DIR"), ".greeter-logged-in", NULL);
        f = fopen (login_lock, "r");
        if (f == NULL)
        {
            if (g_key_file_get_boolean (config, "test-greeter-config", "login-guest", NULL))
            {
                status_notify ("GREETER %s AUTHENTICATE-GUEST", getenv ("DISPLAY"));
                lightdm_greeter_authenticate_as_guest (greeter);
            }
            else if (g_key_file_get_boolean (config, "test-greeter-config", "prompt-username", NULL))
            {
                status_notify ("GREETER %s AUTHENTICATE", getenv ("DISPLAY"));
                lightdm_greeter_authenticate (greeter, NULL);
            }
            else
            {
                gchar *username;

                username = g_key_file_get_string (config, "test-greeter-config", "username", NULL);
                if (username)
                {
                    status_notify ("GREETER %s AUTHENTICATE USERNAME=%s", getenv ("DISPLAY"), username);
                    lightdm_greeter_authenticate (greeter, username);
                    g_free (username);
                }
            }

            /* Write lock to stop repeatedly logging in */
            f = fopen (login_lock, "w");
            fclose (f);
        }
        else
        {
            g_debug ("Not logging in, lock file detected %s", login_lock);
            fclose (f);
        }
    }

    g_main_loop_run (main_loop);

    return EXIT_SUCCESS;
}
