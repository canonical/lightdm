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
    int timeout = lightdm_greeter_get_autologin_timeout_hint (greeter);

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
    {
        if (timeout != 0)
            status_notify ("%s AUTOLOGIN-USER USERNAME=%s TIMEOUT=%d", greeter_id, lightdm_greeter_get_autologin_user_hint (greeter), timeout);
        else
            status_notify ("%s AUTOLOGIN-USER USERNAME=%s", greeter_id, lightdm_greeter_get_autologin_user_hint (greeter));
    }
    else if (lightdm_greeter_get_autologin_guest_hint (greeter))
    {
        if (timeout != 0)
            status_notify ("%s AUTOLOGIN-GUEST TIMEOUT=%d", greeter_id, timeout);
        else
            status_notify ("%s AUTOLOGIN-GUEST", greeter_id);
    }
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
    GError *error = NULL;

    if (!lightdm_greeter_start_session_finish (greeter, result, &error))
        status_notify ("%s SESSION-FAILED", greeter_id);
    g_clear_error (&error);
}

static void
write_shared_data_finished (GObject *object, GAsyncResult *result, gpointer data)
{
    LightDMGreeter *greeter = LIGHTDM_GREETER (object);
    gchar *dir, *path, *test_data;
    FILE *f;

    dir = lightdm_greeter_ensure_shared_data_dir_finish (greeter, result);
    if (!dir)
    {
        status_notify ("%s WRITE-SHARED-DATA ERROR=NO_SHARED_DIR", greeter_id);
        return;
    }

    path = g_build_filename (dir, "data", NULL);
    test_data = data;
    if (!(f = fopen (path, "w")) || fprintf (f, "%s", test_data) < 0)
        status_notify ("%s WRITE-SHARED-DATA ERROR=%s", greeter_id, strerror (errno));
    else
        status_notify ("%s WRITE-SHARED-DATA RESULT=TRUE", greeter_id);
    g_free (test_data);

    if (f)
        fclose (f);
    g_free (path);
    g_free (dir);
}

static void
read_shared_data_finished (GObject *object, GAsyncResult *result, gpointer data)
{
    LightDMGreeter *greeter = LIGHTDM_GREETER (object);
    gchar *dir, *path;
    gchar *contents = NULL;
    GError *error = NULL;

    dir = lightdm_greeter_ensure_shared_data_dir_finish (greeter, result);
    if (!dir)
    {
        status_notify ("%s READ-SHARED-DATA ERROR=NO_SHARED_DIR", greeter_id);
        return;
    }

    path = g_build_filename (dir, "data", NULL);
    if (g_file_get_contents (path, &contents, NULL, &error))
        status_notify ("%s READ-SHARED-DATA DATA=%s", greeter_id, contents);
    else
        status_notify ("%s READ-SHARED-DATA ERROR=%s", greeter_id, error->message);
    g_free (path);
    g_free (contents);
    g_clear_error (&error);
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
        lightdm_greeter_authenticate (greeter, g_hash_table_lookup (params, "USERNAME"));

    else if (strcmp (name, "AUTHENTICATE-GUEST") == 0)
        lightdm_greeter_authenticate_as_guest (greeter);

    else if (strcmp (name, "AUTHENTICATE-AUTOLOGIN") == 0)
        lightdm_greeter_authenticate_autologin (greeter);

    else if (strcmp (name, "AUTHENTICATE-REMOTE") == 0)
        lightdm_greeter_authenticate_remote (greeter, g_hash_table_lookup (params, "SESSION"), NULL);

    else if (strcmp (name, "RESPOND") == 0)
        lightdm_greeter_respond (greeter, g_hash_table_lookup (params, "TEXT"));

    else if (strcmp (name, "CANCEL-AUTHENTICATION") == 0)
        lightdm_greeter_cancel_authentication (greeter);

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
        LightDMUser *user;
        const gchar *username;

        username = g_hash_table_lookup (params, "USERNAME");
        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        if (user)
            g_signal_connect (user, LIGHTDM_SIGNAL_USER_CHANGED, G_CALLBACK (user_changed_cb), NULL);
        status_notify ("%s WATCH-USER USERNAME=%s", greeter_id, username);
    }

    else if (strcmp (name, "LOG-USER") == 0)
    {
        LightDMUser *user;
        const gchar *username, *image, *background, *language, *layout, *session;
        const gchar * const * layouts;
        gchar **fields = NULL;
        gchar *layouts_text;
        GString *status_text;
        int i;

        username = g_hash_table_lookup (params, "USERNAME");
        if (g_hash_table_lookup (params, "FIELDS"))
            fields = g_strsplit (g_hash_table_lookup (params, "FIELDS"), ",", -1);
        if (!fields)
        {
            fields = g_malloc (sizeof (gchar *) * 1);
            fields[0] = NULL;
        }

        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        image = lightdm_user_get_image (user);
        background = lightdm_user_get_background (user);
        language = lightdm_user_get_language (user);
        layout = lightdm_user_get_layout (user);
        layouts = lightdm_user_get_layouts (user);
        layouts_text = g_strjoinv (";", (gchar **) layouts);
        session = lightdm_user_get_session (user);

        status_text = g_string_new ("");
        g_string_append_printf (status_text, "%s LOG-USER USERNAME=%s", greeter_id, username);
        for (i = 0; fields[i]; i++)
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
        g_strfreev (fields);
        g_free (layouts_text);

        status_notify ("%s", status_text->str);
        g_string_free (status_text, TRUE);
    }

    else if (strcmp (name, "LOG-USER-LIST") == 0)
    {
        GList *users, *link;

        users = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
        for (link = users; link; link = link->next)
        {
            LightDMUser *user = link->data;
            status_notify ("%s LOG-USER USERNAME=%s", greeter_id, lightdm_user_get_name (user));
        }
    }

    else if (strcmp (name, "GET-CAN-SUSPEND") == 0)
    {
        gboolean can_suspend = lightdm_get_can_suspend ();
        status_notify ("%s CAN-SUSPEND ALLOWED=%s", greeter_id, can_suspend ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "SUSPEND") == 0)
    {
        GError *error = NULL;
        if (!lightdm_suspend (&error))
            status_notify ("%s FAIL-SUSPEND", greeter_id);
        g_clear_error (&error);
    }

    else if (strcmp (name, "GET-CAN-HIBERNATE") == 0)
    {
        gboolean can_hibernate = lightdm_get_can_hibernate ();
        status_notify ("%s CAN-HIBERNATE ALLOWED=%s", greeter_id, can_hibernate ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "HIBERNATE") == 0)
    {
        GError *error = NULL;
        if (!lightdm_hibernate (&error))
            status_notify ("%s FAIL-HIBERNATE", greeter_id);
        g_clear_error (&error);
    }

    else if (strcmp (name, "GET-CAN-RESTART") == 0)
    {
        gboolean can_restart = lightdm_get_can_restart ();
        status_notify ("%s CAN-RESTART ALLOWED=%s", greeter_id, can_restart ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "RESTART") == 0)
    {
        GError *error = NULL;
        if (!lightdm_restart (&error))
            status_notify ("%s FAIL-RESTART", greeter_id);
        g_clear_error (&error);
    }

    else if (strcmp (name, "GET-CAN-SHUTDOWN") == 0)
    {
        gboolean can_shutdown = lightdm_get_can_shutdown ();
        status_notify ("%s CAN-SHUTDOWN ALLOWED=%s", greeter_id, can_shutdown ? "TRUE" : "FALSE");
    }

    else if (strcmp (name, "SHUTDOWN") == 0)
    {
        GError *error = NULL;
        if (!lightdm_shutdown (&error))
            status_notify ("%s FAIL-SHUTDOWN", greeter_id);
        g_clear_error (&error);
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
    GError *error = NULL;

    if (!lightdm_greeter_connect_to_daemon_finish (greeter, result, &error))
    {
        status_notify ("%s FAIL-CONNECT-DAEMON", greeter_id);
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
    gchar *display, *xdg_seat, *xdg_vtnr, *xdg_session_cookie, *xdg_session_class, *xdg_session_type, *mir_socket, *mir_vt, *mir_id, *path;
    GString *status_text;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    display = getenv ("DISPLAY");
    xdg_seat = getenv ("XDG_SEAT");
    xdg_vtnr = getenv ("XDG_VTNR");
    xdg_session_cookie = getenv ("XDG_SESSION_COOKIE");
    xdg_session_class = getenv ("XDG_SESSION_CLASS");
    xdg_session_type = getenv ("XDG_SESSION_TYPE");  
    mir_socket = getenv ("MIR_SOCKET");
    mir_vt = getenv ("MIR_SERVER_VT");
    mir_id = getenv ("MIR_SERVER_NAME");
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
    else if (g_strcmp0 (xdg_session_type, "wayland") == 0)
        greeter_id = g_strdup ("GREETER-WAYLAND");
    else
        greeter_id = g_strdup ("GREETER-?");

    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGINT, sigint_cb, NULL);
    g_unix_signal_add (SIGTERM, sigterm_cb, NULL);

    status_connect (request_cb, greeter_id);

    status_text = g_string_new ("");
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
    g_string_free (status_text, TRUE);

    config = g_key_file_new ();
    path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL);
    g_key_file_load_from_file (config, path, G_KEY_FILE_NONE, NULL);
    g_free (path);

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

    return exit_code;
}
