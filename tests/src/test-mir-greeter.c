/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lightdm.h>

#include "status.h"

static gchar *greeter_id;
static GMainLoop *loop;
static LightDMGreeter *greeter;
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
signal_cb (int signum)
{
    status_notify ("%s TERMINATE SIGNAL=%d", greeter_id, signum);
    exit (EXIT_SUCCESS);
}

static void
request_cb (const gchar *name, GHashTable *params)
{
    if (!name)
    {
        g_main_loop_quit (loop);
        return;
    }
  
    if (strcmp (name, "AUTHENTICATE") == 0)
        lightdm_greeter_authenticate (greeter, g_hash_table_lookup (params, "USERNAME"));

    if (strcmp (name, "RESPOND") == 0)
    {
        gchar *text = g_strdup (g_hash_table_lookup (params, "TEXT"));
        text[strlen (text) - 1] = '\0';
        lightdm_greeter_respond (greeter, text);
        g_free (text);
    }

    if (strcmp (name, "CANCEL-AUTHENTICATION") == 0)
        lightdm_greeter_cancel_authentication (greeter);

    if (strcmp (name, "START-SESSION") == 0)
    {
        if (!lightdm_greeter_start_session_sync (greeter, g_hash_table_lookup (params, "SESSION"), NULL))
            status_notify ("%s SESSION-FAILED", greeter_id); 
    }
}

int
main (int argc, char **argv)
{
    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    greeter_id = g_strdup ("GREETER-MIR");

    loop = g_main_loop_new (NULL, FALSE);

    status_connect (request_cb, greeter_id);

    status_notify ("%s START", greeter_id);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (g_key_file_has_key (config, "test-greeter-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-greeter-config", "return-value", NULL);
        status_notify ("%s EXIT CODE=%d", greeter_id, return_value);
        return return_value;
    }

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);

    status_notify ("%s CONNECT-TO-DAEMON", greeter_id);
    if (!lightdm_greeter_connect_sync (greeter, NULL))
    {
        status_notify ("%s FAIL-CONNECT-DAEMON", greeter_id);
        return EXIT_FAILURE;
    }

    status_notify ("%s CONNECTED-TO-DAEMON", greeter_id);

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
