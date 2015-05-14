#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <glib-unix.h>

#include "status.h"
#include "x-server.h"
#include "x-authority.h"
#include "xdmcp-client.h"

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

/* VT being run on */
static int vt_number = -1;

/* X server */
static XServer *xserver = NULL;

static void
cleanup (void)
{
    if (lock_path)
        unlink (lock_path);
    if (xserver)
        g_object_unref (xserver);
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
}

int
main (int argc, char **argv)
{
    int i;
    char *pid_string;
    gchar *seat = NULL;
    gchar *mir_id = NULL;
    gchar *lock_filename;
    gboolean sharevts = FALSE;
    int lock_file;
    GString *status_text;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    loop = g_main_loop_new (NULL, FALSE);

    g_unix_signal_add (SIGINT, sigint_cb, NULL);
    g_unix_signal_add (SIGTERM, sigterm_cb, NULL);
    g_unix_signal_add (SIGHUP, sighup_cb, NULL);

    for (i = 1; i < argc; i++)
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
        else if (strcmp (arg, "-nr") == 0)
        {
        }
        else if (strcmp (arg, "-background") == 0)
        {
            /* Ignore arg */
            i++;
        }
        else if (g_str_has_prefix (arg, "vt"))
        {
            vt_number = atoi (arg + 2);
        }
        else if (strcmp (arg, "-novtswitch") == 0)
        {
            /* Ignore VT args */
        }
        else if (strcmp (arg, "-seat") == 0)
        {
            seat = argv[i+1];
            i++;
        }
        else if (strcmp (arg, "-sharevts") == 0)
        {
            sharevts = TRUE;
        }
        else if (strcmp (arg, "-mir") == 0)
        {
            mir_id = argv[i+1];
            i++;
        }
        else if (strcmp (arg, "-mirSocket") == 0)
        {
            /* FIXME */
            i++;
        }
        else
        {
            g_printerr ("Unrecognized option: %s\n"
                        "Use: %s [:<display>] [option]\n"
                        "-auth file             Select authorization file\n"
                        "-nolisten protocol     Don't listen on protocol\n"
                        "-background [none]     Create root window with no background\n"
                        "-nr                    (Ubuntu-specific) Synonym for -background none\n"
                        "-seat string           seat to run on\n"
                        "-sharevts              share VTs with another X server\n"
                        "-mir id                Mir ID to use\n"
                        "-mirSocket name        Mir socket to use\n"
                        "vtxx                   Use virtual terminal xx instead of the next available\n",
                        arg, argv[0]);
            return EXIT_FAILURE;
        }
    }

    id = g_strdup_printf ("XMIR-%d", display_number);

    status_connect (request_cb, id);

    xserver = x_server_new (display_number);
    g_signal_connect (xserver, "client-connected", G_CALLBACK (client_connected_cb), NULL);
    g_signal_connect (xserver, "client-disconnected", G_CALLBACK (client_disconnected_cb), NULL);

    status_text = g_string_new ("");
    g_string_printf (status_text, "%s START", id);
    if (vt_number >= 0)
        g_string_append_printf (status_text, " VT=%d", vt_number);
    if (seat != NULL)
        g_string_append_printf (status_text, " SEAT=%s", seat);
    if (sharevts)
        g_string_append (status_text, " SHAREVTS=TRUE");
    if (mir_id != NULL)
        g_string_append_printf (status_text, " MIR-ID=%s", mir_id);
    status_notify ("%s", status_text->str);
    g_string_free (status_text, TRUE);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (g_key_file_has_key (config, "test-xserver-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-xserver-config", "return-value", NULL);
        status_notify ("%s EXIT CODE=%d", id, return_value);
        return return_value;
    }

    lock_filename = g_strdup_printf (".X%d-lock", display_number);
    lock_path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "tmp", lock_filename, NULL);
    g_free (lock_filename);
    lock_file = open (lock_path, O_CREAT | O_EXCL | O_WRONLY, 0444);
    if (lock_file < 0)
    {
        char *lock_contents = NULL;

        if (g_file_get_contents (lock_path, &lock_contents, NULL, NULL))
        {
            gchar *proc_filename;
            pid_t pid;

            pid = atol (lock_contents);
            g_free (lock_contents);

            proc_filename = g_strdup_printf ("/proc/%d", pid);
            if (!g_file_test (proc_filename, G_FILE_TEST_EXISTS))
            {
                gchar *socket_dir;
                gchar *socket_filename;
                gchar *socket_path;

                socket_dir = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "tmp", ".X11-unix", NULL);
                g_mkdir_with_parents (socket_dir, 0755);

                socket_filename = g_strdup_printf ("X%d", display_number);
                socket_path = g_build_filename (socket_dir, socket_filename, NULL);

                g_printerr ("Breaking lock on non-existant process %d\n", pid);
                unlink (lock_path);
                unlink (socket_path);

                g_free (socket_dir);
                g_free (socket_filename);
                g_free (socket_path);
            }
            g_free (proc_filename);

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
        g_free (lock_path);
        lock_path = NULL;
        return EXIT_FAILURE;
    }
    pid_string = g_strdup_printf ("%10ld", (long) getpid ());
    if (write (lock_file, pid_string, strlen (pid_string)) < 0)
    {
        g_warning ("Error writing PID file: %s", strerror (errno));
        return EXIT_FAILURE;
    }
    g_free (pid_string);

    if (!x_server_start (xserver))
        return EXIT_FAILURE;

    g_main_loop_run (loop);

    cleanup ();

    return exit_status;
}
