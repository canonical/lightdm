#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <xcb/xcb.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "status.h"

static GString *open_fds;

static GKeyFile *config;

static xcb_connection_t *connection;

static void
quit_cb (int signum)
{
    status_notify ("SESSION %s TERMINATE SIGNAL=%d", getenv ("DISPLAY"), signum);
    exit (EXIT_SUCCESS);
}

static void
request_cb (const gchar *request)
{
    gchar *r;
  
    r = g_strdup_printf ("SESSION %s LOGOUT", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
        exit (EXIT_SUCCESS);
    g_free (r);
  
    r = g_strdup_printf ("SESSION %s CRASH", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
        kill (getpid (), SIGSEGV);
    g_free (r);

    r = g_strdup_printf ("SESSION %s LOCK-SEAT", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
    {
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
        status_notify ("SESSION %s LOCK-SEAT", getenv ("DISPLAY"));
    }
    g_free (r);

    r = g_strdup_printf ("SESSION %s LOCK-SESSION", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
    {
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
        status_notify ("SESSION %s LOCK-SESSION", getenv ("DISPLAY"));
    }
    g_free (r);

    r = g_strdup_printf ("SESSION %s LIST-GROUPS", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
    {
        int n_groups, i;
        gid_t *groups;
        GString *group_list;

        n_groups = getgroups (0, NULL);
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
        status_notify ("SESSION %s LIST-GROUPS GROUPS=%s", getenv ("DISPLAY"), group_list->str);
        g_string_free (group_list, TRUE);
        free (groups);
    }

    r = g_strdup_printf ("SESSION %s READ-ENV NAME=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
    {
        const gchar *name = request + strlen (r);
        const gchar *value = g_getenv (name);
        status_notify ("SESSION %s READ-ENV NAME=%s VALUE=%s", getenv ("DISPLAY"), name, value ? value : "");
    }
    g_free (r);

    r = g_strdup_printf ("SESSION %s WRITE-STDOUT TEXT=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
        g_print ("%s\n", request + strlen (r));
    g_free (r);

    r = g_strdup_printf ("SESSION %s WRITE-STDERR TEXT=", getenv ("DISPLAY"));
    if (g_str_has_prefix (request, r))
        g_printerr ("%s\n", request + strlen (r));
    g_free (r);

    r = g_strdup_printf ("SESSION %s READ-XSESSION-ERRORS", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
    {
        gchar *contents;
        GError *error = NULL;

        if (g_file_get_contents (".xsession-errors", &contents, NULL, &error))
            status_notify ("SESSION %s READ-XSESSION-ERRORS TEXT=%s", getenv ("DISPLAY"), contents);
        else
            status_notify ("SESSION %s READ-XSESSION-ERRORS ERROR=%s", getenv ("DISPLAY"), error->message);
        g_clear_error (&error);
    }
    g_free (r);

    r = g_strdup_printf ("SESSION %s LIST-UNKNOWN-FILE-DESCRIPTORS", getenv ("DISPLAY"));
    if (strcmp (request, r) == 0)
        status_notify ("SESSION %s LIST-UNKNOWN-FILE-DESCRIPTORS FDS=%s", getenv ("DISPLAY"), open_fds->str);
    g_free (r);
}

int
main (int argc, char **argv)
{
    GMainLoop *loop;
    int fd, open_max;

    open_fds = g_string_new ("");
    open_max = sysconf (_SC_OPEN_MAX);
    for (fd = STDERR_FILENO + 1; fd < open_max; fd++)
    {
        if (fcntl (fd, F_GETFD) >= 0)
            g_string_append_printf (open_fds, "%d,", fd);
    }
    if (g_str_has_suffix (open_fds->str, ","))
        open_fds->str[strlen (open_fds->str) - 1] = '\0';

    signal (SIGINT, quit_cb);
    signal (SIGTERM, quit_cb);

    g_type_init ();

    loop = g_main_loop_new (NULL, FALSE);

    status_connect (request_cb);

    if (argc > 1)
        status_notify ("SESSION %s START NAME=%s USER=%s", getenv ("DISPLAY"), argv[1], getenv ("USER"));
    else
        status_notify ("SESSION %s START USER=%s", getenv ("DISPLAY"), getenv ("USER"));

    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    connection = xcb_connect (NULL, NULL);

    if (xcb_connection_has_error (connection))
    {
        status_notify ("SESSION %s CONNECT-XSERVER-ERROR", getenv ("DISPLAY"));
        return EXIT_FAILURE;
    }

    status_notify ("SESSION %s CONNECT-XSERVER", getenv ("DISPLAY"));

    g_main_loop_run (loop);    

    return EXIT_SUCCESS;
}
