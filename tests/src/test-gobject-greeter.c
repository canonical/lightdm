#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <lightdm/greeter.h>

#include "status.h"

static xcb_connection_t *connection = NULL;
static GKeyFile *config;

static void
show_message_cb (LightDMGreeter *greeter, const gchar *text, LightDMMessageType type)
{
    notify_status ("GREETER SHOW-MESSAGE TEXT=\"%s\"", text);
}

static void
show_prompt_cb (LightDMGreeter *greeter, const gchar *text, LightDMPromptType type)
{
    gchar *username, *password, *response = NULL;

    notify_status ("GREETER SHOW-PROMPT TEXT=\"%s\"", text);

    username = g_key_file_get_string (config, "test-greeter-config", "username", NULL);
    password = g_key_file_get_string (config, "test-greeter-config", "password", NULL);

    if (g_key_file_get_boolean (config, "test-greeter-config", "prompt-username", NULL) && strcmp (text, "login:") == 0)
        response = username;
    else if (password)
        response = password;

    if (response)
    {
        notify_status ("GREETER RESPOND TEXT=\"%s\"", response);
        lightdm_greeter_respond (greeter, response);
    }

    g_free (username);
    g_free (password);
}

static void
authentication_complete_cb (LightDMGreeter *greeter)
{
    if (lightdm_greeter_get_authentication_user (greeter))
        notify_status ("GREETER AUTHENTICATION-COMPLETE USERNAME=%s AUTHENTICATED=%s",
                       lightdm_greeter_get_authentication_user (greeter),
                       lightdm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
    else
        notify_status ("GREETER AUTHENTICATION-COMPLETE AUTHENTICATED=%s",
                       lightdm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
    if (!lightdm_greeter_get_is_authenticated (greeter))
        return;

    if (!lightdm_greeter_start_session_sync (greeter, NULL))
        notify_status ("GREETER SESSION-FAILED");
}

static void
signal_cb (int signum)
{
    notify_status ("GREETER TERMINATE SIGNAL=%d", signum);
    exit (EXIT_SUCCESS);
}

int
main (int argc, char **argv)
{
    GMainLoop *main_loop;
    LightDMGreeter *greeter;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    notify_status ("GREETER START");

    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    g_type_init ();
    main_loop = g_main_loop_new (NULL, FALSE);

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        fprintf (stderr, "Error connecting to X server\n");
        return EXIT_FAILURE;
    }

    notify_status ("GREETER CONNECT-XSERVER %s", getenv ("DISPLAY"));

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);

    notify_status ("GREETER CONNECT-TO-DAEMON");
    if (!lightdm_greeter_connect_sync (greeter))
        return EXIT_FAILURE;

    notify_status ("GREETER CONNECTED-TO-DAEMON");

    if (g_key_file_get_boolean (config, "test-greeter-config", "crash-xserver", NULL))
    {
        const gchar *name = "SIGSEGV";
        notify_status ("GREETER CRASH-XSERVER");
        xcb_intern_atom (connection, FALSE, strlen (name), name);
        xcb_flush (connection);
    }

    /* Automatically log in as requested user */
    if (lightdm_greeter_get_select_user_hint (greeter))
    {
        notify_status ("GREETER AUTHENTICATE-SELECTED USERNAME=%s", lightdm_greeter_get_select_user_hint (greeter));
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
                notify_status ("GREETER AUTHENTICATE-GUEST");
                lightdm_greeter_authenticate_as_guest (greeter);
            }
            else if (g_key_file_get_boolean (config, "test-greeter-config", "prompt-username", NULL))
            {
                notify_status ("GREETER AUTHENTICATE");
                lightdm_greeter_authenticate (greeter, NULL);
            }
            else
            {
                gchar *username;

                username = g_key_file_get_string (config, "test-greeter-config", "username", NULL);
                if (username)
                {
                    notify_status ("GREETER AUTHENTICATE USERNAME=%s", username);
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
