#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <xcb/xcb.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <lightdm/greeter.h>

#include "status.h"

static gchar *session_id;

static GMainLoop *loop;

static GString *open_fds;

static GKeyFile *config;

static xcb_connection_t *connection;

static LightDMGreeter *greeter = NULL;

static gboolean
sigint_cb (gpointer user_data)
{
    status_notify ("%s TERMINATE SIGNAL=%d", session_id, SIGINT);
    g_main_loop_quit (loop);
    return TRUE;
}

static gboolean
sigterm_cb (gpointer user_data)
{
    status_notify ("%s TERMINATE SIGNAL=%d", session_id, SIGTERM);
    g_main_loop_quit (loop);
    return TRUE;
}

static void
show_message_cb (LightDMGreeter *greeter, const gchar *text, LightDMMessageType type)
{
    status_notify ("%s GREETER-SHOW-MESSAGE TEXT=\"%s\"", session_id, text);
}

static void
show_prompt_cb (LightDMGreeter *greeter, const gchar *text, LightDMPromptType type)
{
    status_notify ("%s GREETER-SHOW-PROMPT TEXT=\"%s\"", session_id, text);
}

static void
authentication_complete_cb (LightDMGreeter *greeter)
{
    if (lightdm_greeter_get_authentication_user (greeter))
        status_notify ("%s GREETER-AUTHENTICATION-COMPLETE USERNAME=%s AUTHENTICATED=%s",
                       session_id,
                       lightdm_greeter_get_authentication_user (greeter),
                       lightdm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
    else
        status_notify ("%s GREETER-AUTHENTICATION-COMPLETE AUTHENTICATED=%s",
                       session_id,
                       lightdm_greeter_get_is_authenticated (greeter) ? "TRUE" : "FALSE");
}

static void
request_cb (const gchar *name, GHashTable *params)
{
    GError *error = NULL;

    if (!name)
    {
        g_main_loop_quit (loop);
        return;
    }

    if (strcmp (name, "LOGOUT") == 0)
        exit (EXIT_SUCCESS);

    else if (strcmp (name, "CRASH") == 0)
        kill (getpid (), SIGSEGV);

    else if (strcmp (name, "LOCK-SEAT") == 0)
    {
        status_notify ("%s LOCK-SEAT", session_id);
        g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                     "org.freedesktop.DisplayManager",
                                     getenv ("XDG_SEAT_PATH"),
                                     "org.freedesktop.DisplayManager.Seat",
                                     "Lock",
                                     g_variant_new ("()"),
                                     G_VARIANT_TYPE ("()"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     1000,
                                     NULL,
                                     NULL);
    }

    else if (strcmp (name, "LOCK-SESSION") == 0)
    {
        status_notify ("%s LOCK-SESSION", session_id);
        g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                     "org.freedesktop.DisplayManager",
                                     getenv ("XDG_SESSION_PATH"),
                                     "org.freedesktop.DisplayManager.Session",
                                     "Lock",
                                     g_variant_new ("()"),
                                     G_VARIANT_TYPE ("()"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     1000,
                                     NULL,
                                     NULL);
    }

    else if (strcmp (name, "LIST-GROUPS") == 0)
    {
        int n_groups, i;
        gid_t *groups;
        GString *group_list;

        n_groups = getgroups (0, NULL);
        if (n_groups < 0)
        {
            g_printerr ("Failed to get groups: %s", strerror (errno));
            n_groups = 0;
        }
        groups = malloc (sizeof (gid_t) * n_groups);
        n_groups = getgroups (n_groups, groups);
        group_list = g_string_new ("");
        for (i = 0; i < n_groups; i++)
        {
            struct group *group;

            if (i != 0)
                g_string_append (group_list, ",");
            group = getgrgid (groups[i]);
            if (group)
                g_string_append (group_list, group->gr_name);
            else
                g_string_append_printf (group_list, "%d", groups[i]);
        }
        status_notify ("%s LIST-GROUPS GROUPS=%s", session_id, group_list->str);
        g_string_free (group_list, TRUE);
        free (groups);
    }

    else if (strcmp (name, "READ-ENV") == 0)
    {
        const gchar *name = g_hash_table_lookup (params, "NAME");
        const gchar *value = g_getenv (name);
        status_notify ("%s READ-ENV NAME=%s VALUE=%s", session_id, name, value ? value : "");
    }

    else if (strcmp (name, "WRITE-STDOUT") == 0)
        g_print ("%s", (const gchar *) g_hash_table_lookup (params, "TEXT"));

    else if (strcmp (name, "WRITE-STDERR") == 0)
        g_printerr ("%s", (const gchar *) g_hash_table_lookup (params, "TEXT"));

    else if (strcmp (name, "READ") == 0)
    {
        const gchar *name = g_hash_table_lookup (params, "FILE");
        gchar *contents = NULL;
        GError *error = NULL;

        if (g_file_get_contents (name, &contents, NULL, &error))
            status_notify ("%s READ FILE=%s TEXT=%s", session_id, name, contents);
        else
            status_notify ("%s READ FILE=%s ERROR=%s", session_id, name, error->message);
        g_free (contents);
        g_clear_error (&error);
    }

    else if (strcmp (name, "LIST-UNKNOWN-FILE-DESCRIPTORS") == 0)
        status_notify ("%s LIST-UNKNOWN-FILE-DESCRIPTORS FDS=%s", session_id, open_fds->str);

    else if (strcmp (name, "CHECK-X-AUTHORITY") == 0)
    {
        gchar *xauthority;
        GStatBuf file_info;
        GString *mode_string;

        xauthority = g_strdup (g_getenv ("XAUTHORITY"));
        if (!xauthority)
            xauthority = g_build_filename (g_get_home_dir (), ".Xauthority", NULL);

        g_stat (xauthority, &file_info);
        g_free (xauthority);

        mode_string = g_string_new ("");
        g_string_append_c (mode_string, file_info.st_mode & S_IRUSR ? 'r' : '-');
        g_string_append_c (mode_string, file_info.st_mode & S_IWUSR ? 'w' : '-');
        g_string_append_c (mode_string, file_info.st_mode & S_IXUSR ? 'x' : '-');
        g_string_append_c (mode_string, file_info.st_mode & S_IRGRP ? 'r' : '-');
        g_string_append_c (mode_string, file_info.st_mode & S_IWGRP ? 'w' : '-');
        g_string_append_c (mode_string, file_info.st_mode & S_IXGRP ? 'x' : '-');
        g_string_append_c (mode_string, file_info.st_mode & S_IROTH ? 'r' : '-');
        g_string_append_c (mode_string, file_info.st_mode & S_IWOTH ? 'w' : '-');
        g_string_append_c (mode_string, file_info.st_mode & S_IXOTH ? 'x' : '-');
        status_notify ("%s CHECK-X-AUTHORITY MODE=%s", session_id, mode_string->str);
        g_string_free (mode_string, TRUE);
    }

    else if (strcmp (name, "WRITE-SHARED-DATA") == 0)
    {
        const gchar *data = g_hash_table_lookup (params, "DATA");
        gchar *dir;

        dir = getenv ("XDG_GREETER_DATA_DIR");
        if (dir)
        {
            gchar *path;
            FILE *f;

            path = g_build_filename (dir, "data", NULL);
            if (!(f = fopen (path, "w")) || fprintf (f, "%s", data) < 0)
                status_notify ("%s WRITE-SHARED-DATA ERROR=%s", session_id, strerror (errno));
            else
                status_notify ("%s WRITE-SHARED-DATA RESULT=TRUE", session_id);

            if (f)
                fclose (f);
            g_free (path);
        }
        else
            status_notify ("%s WRITE-SHARED-DATA ERROR=NO_XDG_GREETER_DATA_DIR", session_id);
    }

    else if (strcmp (name, "READ-SHARED-DATA") == 0)
    {
        gchar *dir;

        dir = getenv ("XDG_GREETER_DATA_DIR");
        if (dir)
        {
            gchar *path;
            gchar *contents = NULL;
            GError *error = NULL;

            path = g_build_filename (dir, "data", NULL);
            if (g_file_get_contents (path, &contents, NULL, &error))
                status_notify ("%s READ-SHARED-DATA DATA=%s", session_id, contents);
            else
                status_notify ("%s WRITE-SHARED-DATA ERROR=%s", session_id, error->message);
            g_free (path);
            g_free (contents);
            g_clear_error (&error);
        }
        else
            status_notify ("%s WRITE-SHARED-DATA ERROR=NO_XDG_GREETER_DATA_DIR", session_id);
    }

    else if (strcmp (name, "GREETER-START") == 0)
    {
        GError *error = NULL;

        g_assert (greeter == NULL);
        greeter = lightdm_greeter_new ();
        g_signal_connect (greeter, LIGHTDM_GREETER_SIGNAL_SHOW_MESSAGE, G_CALLBACK (show_message_cb), NULL);
        g_signal_connect (greeter, LIGHTDM_GREETER_SIGNAL_SHOW_PROMPT, G_CALLBACK (show_prompt_cb), NULL);
        g_signal_connect (greeter, LIGHTDM_GREETER_SIGNAL_AUTHENTICATION_COMPLETE, G_CALLBACK (authentication_complete_cb), NULL);
        if (lightdm_greeter_connect_to_daemon_sync (greeter, &error))
            status_notify ("%s GREETER-STARTED", session_id);
        else
        {
            status_notify ("%s GREETER-FAILED ERROR=%s", session_id, error->message);
            g_clear_error (&error);
        }
    }

    else if (strcmp (name, "GREETER-AUTHENTICATE") == 0)
    {
        if (!lightdm_greeter_authenticate (greeter, g_hash_table_lookup (params, "USERNAME"), &error))
        {
            status_notify ("%s FAIL-AUTHENTICATE ERROR=%s", session_id, error->message);
            g_clear_error (&error);
        }
    }

    else if (strcmp (name, "GREETER-RESPOND") == 0)
    {
        if (!lightdm_greeter_respond (greeter, g_hash_table_lookup (params, "TEXT"), &error))
        {
            status_notify ("%s FAIL-RESPOND ERROR=%s", session_id, error->message);
            g_clear_error (&error);
        }
    }

    else if (strcmp (name, "GREETER-START-SESSION") == 0)
    {
        if (!lightdm_greeter_start_session_sync (greeter, g_hash_table_lookup (params, "SESSION"), &error))
        {
            status_notify ("%s FAIL-START-SESSION ERROR=%s", session_id, error->message);
            g_clear_error (&error);          
        }
    }
}

int
main (int argc, char **argv)
{
    gchar *display, *xdg_seat, *xdg_vtnr, *xdg_current_desktop, *xdg_greeter_data_dir, *xdg_session_cookie, *xdg_session_class, *xdg_session_type, *xdg_session_desktop, *mir_server_host_socket, *mir_vt, *mir_id;
    GString *status_text;
    int fd, open_max;

    display = getenv ("DISPLAY");
    xdg_seat = getenv ("XDG_SEAT");
    xdg_vtnr = getenv ("XDG_VTNR");
    xdg_current_desktop = getenv ("XDG_CURRENT_DESKTOP");
    xdg_greeter_data_dir = getenv ("XDG_GREETER_DATA_DIR");
    xdg_session_cookie = getenv ("XDG_SESSION_COOKIE");
    xdg_session_class = getenv ("XDG_SESSION_CLASS");
    xdg_session_type = getenv ("XDG_SESSION_TYPE");
    xdg_session_desktop = getenv ("XDG_SESSION_DESKTOP");
    mir_server_host_socket = getenv ("MIR_SERVER_HOST_SOCKET");
    mir_vt = getenv ("MIR_SERVER_VT");
    mir_id = getenv ("MIR_SERVER_NAME");
    if (display)
    {
        if (display[0] == ':')
            session_id = g_strdup_printf ("SESSION-X-%s", display + 1);
        else
            session_id = g_strdup_printf ("SESSION-X-%s", display);
    }
    else if (mir_id)
        session_id = g_strdup_printf ("SESSION-MIR-%s", mir_id);
    else if (mir_server_host_socket || mir_vt)
        session_id = g_strdup ("SESSION-MIR");
    else if (g_strcmp0 (xdg_session_type, "wayland") == 0)
        session_id = g_strdup ("SESSION-WAYLAND");
    else
        session_id = g_strdup ("SESSION-UNKNOWN");

    open_fds = g_string_new ("");
    open_max = sysconf (_SC_OPEN_MAX);
    for (fd = STDERR_FILENO + 1; fd < open_max; fd++)
    {
        if (fcntl (fd, F_GETFD) >= 0)
            g_string_append_printf (open_fds, "%d,", fd);
    }
    if (g_str_has_suffix (open_fds->str, ","))
        open_fds->str[strlen (open_fds->str) - 1] = '\0';

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGINT, sigint_cb, NULL);
    g_unix_signal_add (SIGTERM, sigterm_cb, NULL);

    status_connect (request_cb, session_id);

    status_text = g_string_new ("");
    g_string_printf (status_text, "%s START", session_id);
    if (xdg_seat)
        g_string_append_printf (status_text, " XDG_SEAT=%s", xdg_seat);
    if (xdg_vtnr)
        g_string_append_printf (status_text, " XDG_VTNR=%s", xdg_vtnr);
    if (xdg_current_desktop)
        g_string_append_printf (status_text, " XDG_CURRENT_DESKTOP=%s", xdg_current_desktop);
    if (xdg_greeter_data_dir)
        g_string_append_printf (status_text, " XDG_GREETER_DATA_DIR=%s", xdg_greeter_data_dir);
    if (xdg_session_cookie)
        g_string_append_printf (status_text, " XDG_SESSION_COOKIE=%s", xdg_session_cookie);
    if (xdg_session_class)
        g_string_append_printf (status_text, " XDG_SESSION_CLASS=%s", xdg_session_class);
    if (xdg_session_type)
        g_string_append_printf (status_text, " XDG_SESSION_TYPE=%s", xdg_session_type);
    if (xdg_session_desktop)
        g_string_append_printf (status_text, " XDG_SESSION_DESKTOP=%s", xdg_session_desktop);
    if (mir_vt > 0)
        g_string_append_printf (status_text, " MIR_SERVER_VT=%s", mir_vt);
    if (argc > 1)
        g_string_append_printf (status_text, " NAME=%s", argv[1]);
    g_string_append_printf (status_text, " USER=%s", getenv ("USER"));
    status_notify ("%s", status_text->str);
    g_string_free (status_text, TRUE);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (display)
    {
        connection = xcb_connect (NULL, NULL);
        if (xcb_connection_has_error (connection))
        {
            status_notify ("%s CONNECT-XSERVER-ERROR", session_id);
            return EXIT_FAILURE;
        }
        status_notify ("%s CONNECT-XSERVER", session_id);
    }

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
