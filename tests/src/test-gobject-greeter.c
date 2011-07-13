#include <stdlib.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <lightdm/greeter.h>

#include "status.h"

static GKeyFile *config;

static void
connected_cb (LdmGreeter *greeter)
{  
    notify_status ("GREETER CONNECTED-TO-DAEMON");

    if (ldm_greeter_get_is_first (greeter))
    {
        gchar *username;

        username = g_key_file_get_string (config, "test-greeter-config", "username", NULL);

        if (g_key_file_get_boolean (config, "test-greeter-config", "login-guest", NULL))
        {
            notify_status ("GREETER LOGIN-GUEST");
            ldm_greeter_login_as_guest (greeter);
        }
        else if (g_key_file_get_boolean (config, "test-greeter-config", "prompt-username", NULL))
        {
            notify_status ("GREETER LOGIN");
            ldm_greeter_login (greeter, NULL);
        }
        else if (username)
        {
            notify_status ("GREETER LOGIN USERNAME=%s", username);
            ldm_greeter_login (greeter, username);
        }

        g_free (username);
    }
}

static void
show_message_cb (LdmGreeter *greeter, const gchar *text, LdmMessageType type)
{
    notify_status ("GREETER SHOW-MESSAGE TEXT=\"%s\"", text);
}

static void
show_prompt_cb (LdmGreeter *greeter, const gchar *text, LdmPromptType type)
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
        ldm_greeter_respond (greeter, response);
    }

    g_free (username);
    g_free (password);
}

static void
authentication_complete_cb (LdmGreeter *greeter)
{
    notify_status ("GREETER AUTHENTICATION-COMPLETE AUTHENTICATED=%s", ldm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
    if (ldm_greeter_get_is_authenticated (greeter))
        ldm_greeter_start_default_session (greeter);
}

static void
quit_cb (LdmGreeter *greeter)
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
    LdmGreeter *greeter;
    xcb_connection_t *connection;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

    notify_status ("GREETER START");

    config = g_key_file_new ();
    if (g_getenv ("TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    g_type_init ();

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        fprintf (stderr, "Error connecting\n");
        return EXIT_FAILURE;
    }

    notify_status ("GREETER CONNECT-XSERVER %s", getenv ("DISPLAY"));

    greeter = ldm_greeter_new ();
    g_signal_connect (greeter, "connected", G_CALLBACK (connected_cb), NULL);
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "quit", G_CALLBACK (quit_cb), NULL);

    notify_status ("GREETER CONNECT-TO-DAEMON");
    ldm_greeter_connect_to_server (greeter);

    g_main_loop_run (g_main_loop_new (NULL, FALSE));

    return EXIT_SUCCESS;
}
