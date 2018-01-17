/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>
#include <lightdm.h>
#include <glib-unix.h>
#include <ctype.h>

#include "status.h"

static int exit_code = EXIT_SUCCESS;
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
notify_hints (LightDMGreeter *greeter)
{
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
    if (lightdm_greeter_get_autologin_user_hint (greeter))
        status_notify ("%s AUTOLOGIN-USER-HINT=%s", greeter_id, lightdm_greeter_get_autologin_user_hint (greeter));
    if (lightdm_greeter_get_autologin_guest_hint (greeter))
        status_notify ("%s AUTOLOGIN-GUEST-HINT", greeter_id);
    if (lightdm_greeter_get_autologin_session_hint (greeter))
        status_notify ("%s AUTOLOGIN-SESSION-HINT=%s", greeter_id, lightdm_greeter_get_autologin_session_hint (greeter));
    if (lightdm_greeter_get_autologin_timeout_hint (greeter) != 0)
        status_notify ("%s AUTOLOGIN-TIMEOUT-HINT=%d", greeter_id, lightdm_greeter_get_autologin_timeout_hint (greeter));
}

static void
idle_cb (LightDMGreeter *greeter)
{
    status_notify ("%s IDLE", greeter_id);
}

static void
reset_cb (LightDMGreeter *greeter)
{
    status_notify ("%s RESET", greeter_id);
    notify_hints (greeter);
}

static void
user_changed_cb (LightDMUser *user)
{
    status_notify ("%s USER-CHANGED USERNAME=%s", greeter_id, lightdm_user_get_name (user));
}

static void
start_session_finished (GObject *object, GAsyncResult *result, gpointer data)
{
    LightDMGreeter *greeter = LIGHTDM_GREETER (object);
    g_autoptr(GError) error = NULL;

    if (!lightdm_greeter_start_session_finish (greeter, result, &error))
        status_notify ("%s SESSION-FAILED ERROR=%s", greeter_id, error->message);
}

static void
write_shared_data_finished (GObject *object, GAsyncResult *result, gpointer data)
{
    LightDMGreeter *greeter = LIGHTDM_GREETER (object);
    g_autofree gchar *test_data = data;

    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = lightdm_greeter_ensure_shared_data_dir_finish (greeter, result, &error);
    if (!dir)
    {
        status_notify ("%s WRITE-SHARED-DATA ERROR=%s", greeter_id, error->message);
        return;
    }

    g_autofree gchar *path = g_build_filename (dir, "data", NULL);
    FILE *f;
    if (!(f = fopen (path, "w")) || fprintf (f, "%s", test_data) < 0)
        status_notify ("%s WRITE-SHARED-DATA ERROR=%s", greeter_id, strerror (errno));
    else
        status_notify ("%s WRITE-SHARED-DATA RESULT=TRUE", greeter_id);

    if (f)
        fclose (f);
}

static void
read_shared_data_finished (GObject *object, GAsyncResult *result, gpointer data)
{
    LightDMGreeter *greeter = LIGHTDM_GREETER (object);

    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = lightdm_greeter_ensure_shared_data_dir_finish (greeter, result, &error);
    if (!dir)
    {
        status_notify ("%s READ-SHARED-DATA ERROR=%s", greeter_id, error->message);
        return;
    }

    g_autofree gchar *path = g_build_filename (dir, "data", NULL);
    g_autofree gchar *contents = NULL;
    if (g_file_get_contents (path, &contents, NULL, &error))
        status_notify ("%s READ-SHARED-DATA DATA=%s", greeter_id, contents);
    else
        status_notify ("%s READ-SHARED-DATA ERROR=%s", greeter_id, error->message);
}

static void
request_cb (const gchar *name, GHashTable *params)
{
    if (!name)
    {
        g_main_loop_quit (loop);
        return;
    }

    if (strcmp (name, "CRASH") == 0)
        kill (getpid (), SIGSEGV);

    else if (strcmp (name, "AUTHENTICATE") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_greeter_authenticate (greeter, g_hash_table_lookup (params, "USERNAME"), &error))
            status_notify ("%s FAIL-AUTHENTICATE ERROR=%s", greeter_id, error->message);
    }

    else if (strcmp (name, "AUTHENTICATE-GUEST") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_greeter_authenticate_as_guest (greeter, &error))
            status_notify ("%s FAIL-AUTHENTICATE-GUEST ERROR=%s", greeter_id, error->message);
    }

    else if (strcmp (name, "AUTHENTICATE-AUTOLOGIN") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_greeter_authenticate_autologin (greeter, &error))
            status_notify ("%s FAIL-AUTHENTICATE-AUTOLOGIN ERROR=%s", greeter_id, error->message);
    }

    else if (strcmp (name, "AUTHENTICATE-REMOTE") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_greeter_authenticate_remote (greeter, g_hash_table_lookup (params, "SESSION"), NULL, &error))
            status_notify ("%s FAIL-AUTHENTICATE-REMOTE ERROR=%s", greeter_id, error->message);
    }

    else if (strcmp (name, "RESPOND") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_greeter_respond (greeter, g_hash_table_lookup (params, "TEXT"), &error))
            status_notify ("%s FAIL-RESPOND ERROR=%s", greeter_id, error->message);
    }

    else if (strcmp (name, "CANCEL-AUTHENTICATION") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_greeter_cancel_authentication (greeter, &error))
            status_notify ("%s FAIL-CANCEL-AUTHENTICATION ERROR=%s", greeter_id, error->message);
    }

    else if (strcmp (name, "START-SESSION") == 0)
        lightdm_greeter_start_session (greeter, g_hash_table_lookup (params, "SESSION"), NULL, start_session_finished, NULL);

    else if (strcmp (name, "LOG-DEFAULT-SESSION") == 0)
        status_notify ("%s LOG-DEFAULT-SESSION SESSION=%s", greeter_id, lightdm_greeter_get_default_session_hint (greeter));

    else if (strcmp (name, "LOG-USER-LIST-LENGTH") == 0)
        status_notify ("%s LOG-USER-LIST-LENGTH N=%d", greeter_id, lightdm_user_list_get_length (lightdm_user_list_get_instance ()));

    else if (strcmp (name, "WRITE-SHARED-DATA") == 0)
    {
        const gchar *data = g_hash_table_lookup (params, "DATA");
        lightdm_greeter_ensure_shared_data_dir (greeter, g_hash_table_lookup (params, "USERNAME"), NULL, write_shared_data_finished, g_strdup (data));
    }

    else if (strcmp (name, "READ-SHARED-DATA") == 0)
        lightdm_greeter_ensure_shared_data_dir (greeter, g_hash_table_lookup (params, "USERNAME"), NULL, read_shared_data_finished, NULL);

    else if (strcmp (name, "WATCH-USER") == 0)
    {
        const gchar *username = g_hash_table_lookup (params, "USERNAME");
        LightDMUser *user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        if (user)
            g_signal_connect (user, LIGHTDM_SIGNAL_USER_CHANGED, G_CALLBACK (user_changed_cb), NULL);
        status_notify ("%s WATCH-USER USERNAME=%s", greeter_id, username);
    }

    else if (strcmp (name, "LOG-USER") == 0)
    {
        const gchar *username = g_hash_table_lookup (params, "USERNAME");
        g_auto(GStrv) fields = NULL;
        if (g_hash_table_lookup (params, "FIELDS"))
            fields = g_strsplit (g_hash_table_lookup (params, "FIELDS"), ",", -1);
        if (!fields)
        {
            fields = g_malloc (sizeof (gchar *) * 1);
            fields[0] = NULL;
        }

        LightDMUser *user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        const gchar *image = lightdm_user_get_image (user);
        const gchar *background = lightdm_user_get_background (user);
        const gchar *language = lightdm_user_get_language (user);
        const gchar *layout = lightdm_user_get_layout (user);
        const gchar * const * layouts = lightdm_user_get_layouts (user);
        g_autofree gchar *layouts_text = g_strjoinv (";", (gchar **) layouts);
        const gchar *session = lightdm_user_get_session (user);

        g_autoptr(GString) status_text = g_string_new ("");
        g_string_append_printf (status_text, "%s LOG-USER USERNAME=%s", greeter_id, username);
        for (int i = 0; fields[i]; i++)
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
            else if (strcmp (fields[i], "UID") == 0)
                g_string_append_printf (status_text, " UID=%d", lightdm_user_get_uid (user));
        }

        status_notify ("%s", status_text->str);
    }

    else if (strcmp (name, "LOG-USER-LIST") == 0)
    {
        GList *users = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
        for (GList *link = users; link; link = link->next)
        {
            LightDMUser *user = link->data;
            status_notify ("%s LOG-USER USERNAME=%s", greeter_id, lightdm_user_get_name (user));
        }
    }

    else if (strcmp (name, "LOG-SESSIONS") == 0)
    {
        GList *sessions = lightdm_get_sessions ();
        for (GList *link = sessions; link; link = link->next)
        {
            LightDMSession *session = link->data;
            status_notify ("%s LOG-SESSION KEY=%s", greeter_id, lightdm_session_get_key (session));
        }
    }

    else if (strcmp (name, "GET-CAN-SUSPEND") == 0)
    {
        gboolean can_suspend = lightdm_get_can_suspend ();
        status_notify ("%s CAN-SUSPEND ALLOWED=%s", greeter_id, can_suspend ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "SUSPEND") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_suspend (&error))
            status_notify ("%s FAIL-SUSPEND", greeter_id);
    }

    else if (strcmp (name, "GET-CAN-HIBERNATE") == 0)
    {
        gboolean can_hibernate = lightdm_get_can_hibernate ();
        status_notify ("%s CAN-HIBERNATE ALLOWED=%s", greeter_id, can_hibernate ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "HIBERNATE") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_hibernate (&error))
            status_notify ("%s FAIL-HIBERNATE", greeter_id);
    }

    else if (strcmp (name, "GET-CAN-RESTART") == 0)
    {
        gboolean can_restart = lightdm_get_can_restart ();
        status_notify ("%s CAN-RESTART ALLOWED=%s", greeter_id, can_restart ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "RESTART") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_restart (&error))
            status_notify ("%s FAIL-RESTART", greeter_id);
    }

    else if (strcmp (name, "GET-CAN-SHUTDOWN") == 0)
    {
        gboolean can_shutdown = lightdm_get_can_shutdown ();
        status_notify ("%s CAN-SHUTDOWN ALLOWED=%s", greeter_id, can_shutdown ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "SHUTDOWN") == 0)
    {
        g_autoptr(GError) error = NULL;
        if (!lightdm_shutdown (&error))
            status_notify ("%s FAIL-SHUTDOWN", greeter_id);
    }
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

static void
connect_finished (GObject *object, GAsyncResult *result, gpointer data)
{
    LightDMGreeter *greeter = LIGHTDM_GREETER (object);
    g_autoptr(GError) error = NULL;

    if (!lightdm_greeter_connect_to_daemon_finish (greeter, result, &error))
    {
        status_notify ("%s FAIL-CONNECT-DAEMON ERROR=%s", greeter_id, error->message);
        exit_code = EXIT_FAILURE;
        g_main_loop_quit (loop);
        return;
    }

    status_notify ("%s CONNECTED-TO-DAEMON", greeter_id);

    notify_hints (greeter);
}

int
main (int argc, char **argv)
{
#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    const gchar *display = getenv ("DISPLAY");
    const gchar *xdg_seat = getenv ("XDG_SEAT");
    const gchar *xdg_vtnr = getenv ("XDG_VTNR");
    const gchar *xdg_session_cookie = getenv ("XDG_SESSION_COOKIE");
    const gchar *xdg_session_class = getenv ("XDG_SESSION_CLASS");
    const gchar *xdg_session_type = getenv ("XDG_SESSION_TYPE");
    const gchar *mir_server_host_socket = getenv ("MIR_SERVER_HOST_SOCKET");
    const gchar *mir_vt = getenv ("MIR_SERVER_VT");
    const gchar *mir_id = getenv ("MIR_SERVER_NAME");
    if (display)
    {
        if (display[0] == ':')
            greeter_id = g_strdup_printf ("GREETER-X-%s", display + 1);
        else
            greeter_id = g_strdup_printf ("GREETER-X-%s", display);
    }
    else if (mir_id)
        greeter_id = g_strdup_printf ("GREETER-MIR-%s", mir_id);
    else if (mir_server_host_socket || mir_vt)
        greeter_id = g_strdup ("GREETER-MIR");
    else if (g_strcmp0 (xdg_session_type, "wayland") == 0)
        greeter_id = g_strdup ("GREETER-WAYLAND");
    else
        greeter_id = g_strdup ("GREETER-?");

    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGINT, sigint_cb, NULL);
    g_unix_signal_add (SIGTERM, sigterm_cb, NULL);

    status_connect (request_cb, greeter_id);

    g_autoptr(GString) status_text = g_string_new ("");
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
    status_notify ("%s", status_text->str);

    config = g_key_file_new ();
    g_autofree gchar *path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL);
    g_key_file_load_from_file (config, path, G_KEY_FILE_NONE, NULL);

    if (g_key_file_get_boolean (config, "test-greeter-config", "exit-on-startup", NULL))
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
    g_signal_connect (greeter, LIGHTDM_GREETER_SIGNAL_SHOW_MESSAGE, G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, LIGHTDM_GREETER_SIGNAL_SHOW_PROMPT, G_CALLBACK (show_prompt_cb), NULL);
    g_signal_connect (greeter, LIGHTDM_GREETER_SIGNAL_AUTHENTICATION_COMPLETE, G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, LIGHTDM_GREETER_SIGNAL_AUTOLOGIN_TIMER_EXPIRED, G_CALLBACK (autologin_timer_expired_cb), NULL);
    if (g_key_file_get_boolean (config, "test-greeter-config", "resettable", NULL))
    {
        lightdm_greeter_set_resettable (greeter, TRUE);
        g_signal_connect (greeter, LIGHTDM_GREETER_SIGNAL_IDLE, G_CALLBACK (idle_cb), NULL);
        g_signal_connect (greeter, LIGHTDM_GREETER_SIGNAL_RESET, G_CALLBACK (reset_cb), NULL);
    }

    if (g_key_file_get_boolean (config, "test-greeter-config", "log-user-changes", NULL))
    {
        g_signal_connect (lightdm_user_list_get_instance (), LIGHTDM_USER_LIST_SIGNAL_USER_ADDED, G_CALLBACK (user_added_cb), NULL);
        g_signal_connect (lightdm_user_list_get_instance (), LIGHTDM_USER_LIST_SIGNAL_USER_REMOVED, G_CALLBACK (user_removed_cb), NULL);
    }

    status_notify ("%s CONNECT-TO-DAEMON", greeter_id);
    lightdm_greeter_connect_to_daemon (greeter, NULL, connect_finished, NULL);

    g_main_loop_run (loop);

    if (g_key_file_has_key (config, "test-greeter-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-greeter-config", "return-value", NULL);
        return return_value;
    }

    return exit_code;
}
