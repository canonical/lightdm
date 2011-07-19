#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <lightdm/greeter.h>

#include "status.h"

static xcb_connection_t *connection = NULL;
static GKeyFile *config;

static void
connected_cb (LightDMGreeter *greeter)
{
    gchar *login_lock;
    FILE *f;

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
        notify_status ("GREETER LOGIN-SELECTED USERNAME=%s", lightdm_greeter_get_select_user_hint (greeter));
        lightdm_greeter_login (greeter, lightdm_greeter_get_select_user_hint (greeter));
        return;
    }

    login_lock = g_build_filename (g_getenv ("LIGHTDM_TEST_HOME_DIR"), ".greeter-logged-in", NULL);
    f = fopen (login_lock, "r");
    if (f == NULL)
    {
        if (g_key_file_get_boolean (config, "test-greeter-config", "login-guest", NULL))
        {
            notify_status ("GREETER LOGIN-GUEST");
            lightdm_greeter_login_as_guest (greeter);
        }
        else if (g_key_file_get_boolean (config, "test-greeter-config", "prompt-username", NULL))
        {
            notify_status ("GREETER LOGIN");
            lightdm_greeter_login (greeter, NULL);
        }
        else
        {
            gchar *username;

            username = g_key_file_get_string (config, "test-greeter-config", "username", NULL);
            if (!username)
                return;

            notify_status ("GREETER LOGIN USERNAME=%s", username);
            lightdm_greeter_login (greeter, username);
            g_free (username);
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

    if (g_key_file_get_boolean (config, "test-greeter-config", "prompt-username", NULL))
    {
        g_key_file_set_boolean (config, "test-greeter-config", "prompt-username", FALSE);
        response = username;
    }
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
    notify_status ("GREETER AUTHENTICATION-COMPLETE AUTHENTICATED=%s", lightdm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
    if (lightdm_greeter_get_is_authenticated (greeter))
        lightdm_greeter_start_default_session (greeter);
}

static void
session_failed_cb (LightDMGreeter *greeter)
{
    notify_status ("GREETER SESSION-FAILED");
}

static void
quit_cb (LightDMGreeter *greeter)
{
    notify_status ("GREETER QUIT");
    exit (EXIT_SUCCESS);
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
    LightDMGreeter *greeter;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    notify_status ("GREETER START");

    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    g_type_init ();

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        fprintf (stderr, "Error connecting\n");
        return EXIT_FAILURE;
    }

    notify_status ("GREETER CONNECT-XSERVER %s", getenv ("DISPLAY"));

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "connected", G_CALLBACK (connected_cb), NULL);
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "session-failed", G_CALLBACK (session_failed_cb), NULL);
    g_signal_connect (greeter, "quit", G_CALLBACK (quit_cb), NULL);

    notify_status ("GREETER CONNECT-TO-DAEMON");
    lightdm_greeter_connect_to_server (greeter);

    g_main_loop_run (g_main_loop_new (NULL, FALSE));

    return EXIT_SUCCESS;
}
