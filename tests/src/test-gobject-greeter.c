#include <stdlib.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <lightdm/greeter.h>

#include "status.h"

static void
connected_cb (LdmGreeter *greeter)
{  
    notify_status ("GREETER CONNECTED-TO-DAEMON");

    if (ldm_greeter_get_is_first (greeter))
    {
        gchar *username;

        username = ldm_greeter_get_string_property (greeter, "username");

        if (ldm_greeter_get_boolean_property (greeter, "login-guest"))
        {
            notify_status ("GREETER LOGIN-GUEST");
            ldm_greeter_login_as_guest (greeter);
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
show_message_cb (LdmGreeter *greeter, const gchar *text)
{
    notify_status ("GREETER SHOW-MESSAGE TEXT=\"%s\"", text);
}

static void
show_error_cb (LdmGreeter *greeter, const gchar *text)
{
    notify_status ("GREETER SHOW-ERROR TEXT=\"%s\"", text);
}

static void
show_prompt_cb (LdmGreeter *greeter, const gchar *text)
{
    gchar *password;

    notify_status ("GREETER SHOW-PROMPT TEXT=\"%s\"", text);

    password = ldm_greeter_get_string_property (greeter, "password");
    if (password)
    {
        notify_status ("GREETER PROVIDE-SECRET TEXT=\"%s\"", password);
        ldm_greeter_provide_secret (greeter, password);
    }
}

static void
authentication_complete_cb (LdmGreeter *greeter)
{
    notify_status ("GREETER AUTHENTICATION-COMPLETE AUTHENTICATED=%s", ldm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
    if (ldm_greeter_get_is_authenticated (greeter))
        ldm_greeter_start_session_with_defaults (greeter);
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

    g_debug ("Starting greeter");

    notify_status ("GREETER START");

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
    g_signal_connect (greeter, "show-error", G_CALLBACK (show_error_cb), NULL);
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "quit", G_CALLBACK (quit_cb), NULL);

    notify_status ("GREETER CONNECT-TO-DAEMON");
    ldm_greeter_connect_to_server (greeter);

    g_main_loop_run (g_main_loop_new (NULL, FALSE));

    return EXIT_SUCCESS;
}
