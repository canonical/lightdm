#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <gio/gio.h>
#include <glib-unix.h>

#include "status.h"
#include "x-server.h"
#include "x-authority.h"

static GMainLoop *loop;
static int exit_status = EXIT_SUCCESS;

static GKeyFile *config;

/* Path to lock file */
static gchar *lock_path = NULL;

/* Path to authority database to use */
static gchar *auth_path = NULL;

/* ID to use for test reporting */
static gchar *id;

/* Display number being served */
static int display_number = 0;

/* X server */
static XServer *xserver = NULL;

static void
cleanup (void)
{
    if (lock_path)
        unlink (lock_path);
    g_clear_object (&xserver);
}

static void
quit (int status)
{
    exit_status = status;
    g_main_loop_quit (loop);
}

static gboolean
sighup_cb (gpointer user_data)
{
    status_notify ("%s DISCONNECT-CLIENTS", id);
    return TRUE;
}

static gboolean
sigint_cb (gpointer user_data)
{
    status_notify ("%s TERMINATE SIGNAL=%d", id, SIGINT);
    quit (EXIT_SUCCESS);
    return TRUE;
}

static gboolean
sigterm_cb (gpointer user_data)
{
    status_notify ("%s TERMINATE SIGNAL=%d", id, SIGTERM);
    quit (EXIT_SUCCESS);
    return TRUE;
}

static void
client_connected_cb (XServer *server, XClient *client)
{
    status_notify ("%s ACCEPT-CONNECT", id);
    x_client_send_success (client);
}

static void
client_disconnected_cb (XServer *server, XClient *client)
{
    g_signal_handlers_disconnect_matched (client, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, NULL);
}

static gboolean
vnc_data_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    gchar buffer[1024];
    gsize n_read;
    g_autoptr(GError) error = NULL;
    GIOStatus status = g_io_channel_read_chars (channel, buffer, 1023, &n_read, &error);
    if (error)
        g_warning ("Error reading from VNC client: %s", error->message);

    if (status == G_IO_STATUS_NORMAL)
    {
        buffer[n_read] = '\0';
        if (g_str_has_suffix (buffer, "\n"))
            buffer[n_read-1] = '\0';
        status_notify ("%s VNC-CLIENT-CONNECT VERSION=\"%s\"", id, buffer);
    }

    return TRUE;
}

static void
request_cb (const gchar *name, GHashTable *params)
{
    if (!name)
    {
        g_main_loop_quit (loop);
        return;
    }

    if (strcmp (name, "INDICATE-READY") == 0)
    {
        void *handler;

        handler = signal (SIGUSR1, SIG_IGN);
        if (handler == SIG_IGN)
        {
            status_notify ("%s INDICATE-READY", id);
            kill (getppid (), SIGUSR1);
        }
        signal (SIGUSR1, handler);
    }

    else if (strcmp (name, "START-VNC") == 0)
    {
        /* Send server protocol version to client */
        g_print ("RFB 003.007\n");
    }
}

int
main (int argc, char **argv)
{
#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGINT, sigint_cb, NULL);
    g_unix_signal_add (SIGTERM, sigterm_cb, NULL);
    g_unix_signal_add (SIGHUP, sighup_cb, NULL);

    gboolean use_inetd = FALSE;
    gboolean has_option = FALSE;
    const gchar *geometry = "640x480";
    gint depth = 8;
    for (int i = 1; i < argc; i++)
    {
        char *arg = argv[i];

        if (arg[0] == ':')
        {
            display_number = atoi (arg + 1);
        }
        else if (strcmp (arg, "-auth") == 0)
        {
            auth_path = argv[i+1];
            i++;
        }
        else if (strcmp (arg, "-nolisten") == 0)
        {
            char *protocol = argv[i+1];
            i++;
            if (strcmp (protocol, "tcp") == 0)
                ;//listen_tcp = FALSE;
            else if (strcmp (protocol, "unix") == 0)
                ;//listen_unix = FALSE;
        }
        else if (strcmp (arg, "-geometry") == 0)
        {
            geometry = argv[i+1];
            i++;
        }
        else if (strcmp (arg, "-depth") == 0)
        {
            depth = atoi (argv[i+1]);
            i++;
        }
        else if (strcmp (arg, "-inetd") == 0)
        {
            use_inetd = TRUE;
        }
        else if (strcmp (arg, "-option") == 0)
        {
            has_option = TRUE;
        }
        else
        {
            g_printerr ("Unrecognized option: %s\n"
                        "Use: %s [:<display>] [option]\n"
                        "-auth file             Select authorization file\n"
                        "-nolisten protocol     Don't listen on protocol\n"
                        "-geometry WxH          Set framebuffer width & height\n"
                        "-depth D               Set framebuffer depth\n"
                        "-inetd                 Xvnc is launched by inetd\n",
                        arg, argv[0]);
            return EXIT_FAILURE;
        }
    }

    id = g_strdup_printf ("XVNC-%d", display_number);

    status_connect (request_cb, id);

    xserver = x_server_new (display_number);
    g_signal_connect (xserver, X_SERVER_SIGNAL_CLIENT_CONNECTED, G_CALLBACK (client_connected_cb), NULL);
    g_signal_connect (xserver, X_SERVER_SIGNAL_CLIENT_DISCONNECTED, G_CALLBACK (client_disconnected_cb), NULL);

    status_notify ("%s START GEOMETRY=%s DEPTH=%d OPTION=%s", id, geometry, depth, has_option ? "TRUE" : "FALSE");

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (use_inetd)
    {
        if (!g_io_add_watch (g_io_channel_unix_new (STDIN_FILENO), G_IO_IN, vnc_data_cb, NULL))
            return EXIT_FAILURE;
    }
    else
    {
        g_printerr ("Only supported in -inetd mode\n");
        return EXIT_FAILURE;
    }

    g_autofree gchar *lock_filename = g_strdup_printf (".X%d-lock", display_number);
    lock_path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "tmp", lock_filename, NULL);
    int lock_file = open (lock_path, O_CREAT | O_EXCL | O_WRONLY, 0444);
    if (lock_file < 0)
    {
        g_autofree gchar *lock_contents = NULL;
        if (g_file_get_contents (lock_path, &lock_contents, NULL, NULL))
        {
            pid_t pid = atol (lock_contents);

            g_autofree gchar *proc_filename = g_strdup_printf ("/proc/%d", pid);
            if (!g_file_test (proc_filename, G_FILE_TEST_EXISTS))
            {
                g_autofree gchar *socket_dir = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "tmp", ".X11-unix", NULL);
                g_mkdir_with_parents (socket_dir, 0755);

                g_autofree gchar *socket_filename = g_strdup_printf ("X%d", display_number);
                g_autofree gchar *socket_path = g_build_filename (socket_dir, socket_filename, NULL);

                g_printerr ("Breaking lock on non-existant process %d\n", pid);
                unlink (lock_path);
                unlink (socket_path);
            }

            lock_file = open (lock_path, O_CREAT | O_EXCL | O_WRONLY, 0444);
        }
    }
    if (lock_file < 0)
    {
        fprintf (stderr,
                 "Fatal server error:\n"
                 "Server is already active for display %d\n"
                 "	If this server is no longer running, remove %s\n"
                 "	and start again.\n", display_number, lock_path);
        g_clear_pointer (&lock_path, g_free);
        return EXIT_FAILURE;
    }
    g_autofree gchar *pid_string = g_strdup_printf ("%10ld", (long) getpid ());
    if (write (lock_file, pid_string, strlen (pid_string)) < 0)
    {
        g_warning ("Error writing PID file: %s", strerror (errno));
        return EXIT_FAILURE;
    }

    if (!x_server_start (xserver))
        return EXIT_FAILURE;

    g_main_loop_run (loop);

    cleanup ();

    return exit_status;
}
