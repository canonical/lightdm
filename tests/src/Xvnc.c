#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <gio/gio.h>

#include "status.h"
#include "x-server.h"
#include "x-authority.h"

static GMainLoop *loop;

static GKeyFile *config;

/* Path to lock file */
static gchar *lock_path = NULL;

/* Path to authority database to use */
static gchar *auth_path = NULL;

/* Display number being served */
static int display_number = 0;

/* X server */
static XServer *xserver = NULL;

static void
indicate_ready ()
{
    void *handler;  
    handler = signal (SIGUSR1, SIG_IGN);
    if (handler == SIG_IGN)
    {
        status_notify ("XSERVER-%d INDICATE-READY", display_number);
        kill (getppid (), SIGUSR1);
    }
    signal (SIGUSR1, handler);
}

static void
cleanup ()
{
    if (lock_path)
        unlink (lock_path);
    if (xserver)
        g_object_unref (xserver);
}

static void
quit (int status)
{
    cleanup ();
    exit (status);
}

static void
signal_cb (int signum)
{
    if (signum == SIGHUP)
    {
        status_notify ("XSERVER-%d DISCONNECT-CLIENTS", display_number);
        indicate_ready ();
    }
    else
    {
        status_notify ("XSERVER-%d TERMINATE SIGNAL=%d", display_number, signum);
        quit (EXIT_SUCCESS);
    }
}

static void
x_client_connect_cb (XClient *client, XConnect *message)
{
    gchar *auth_error = NULL;

    if (x_client_get_address (client))
        status_notify ("XSERVER-%d TCP-ACCEPT-CONNECT", display_number);
    else
        status_notify ("XSERVER-%d ACCEPT-CONNECT", display_number);

    if (auth_path)
    {
        XAuthority *authority;
        XAuthorityRecord *record = NULL;
        GError *error = NULL;

        authority = x_authority_new ();
        x_authority_load (authority, auth_path, &error);
        if (error)
            g_warning ("Error reading auth file: %s", error->message);
        g_clear_error (&error);

        if (x_client_get_address (client))
            record = x_authority_match_localhost (authority, message->authorization_protocol_name); // FIXME: Should check if remote
        else
            record = x_authority_match_local (authority, message->authorization_protocol_name);
        if (record)
        {
            if (strcmp (message->authorization_protocol_name, "MIT-MAGIC-COOKIE-1") == 0)
            {
                if (!x_authority_record_check_cookie (record, message->authorization_protocol_data, message->authorization_protocol_data_length))
                    auth_error = g_strdup_printf ("Invalid MIT-MAGIC-COOKIE key");
            }
            else
                auth_error = g_strdup_printf ("Unknown authorization: '%s'", message->authorization_protocol_name);
        }
        else
            auth_error = g_strdup ("No authorization record");
    }

    if (auth_error)
        x_client_send_failed (client, auth_error);
    else
        x_client_send_success (client);
    g_free (auth_error);
}

static void
client_connected_cb (XServer *server, XClient *client)
{
    g_signal_connect (client, "connect", G_CALLBACK (x_client_connect_cb), NULL);
}

static void
client_disconnected_cb (XServer *server, XClient *client)
{  
    g_signal_handlers_disconnect_matched (client, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, NULL);  
    if (x_server_get_n_clients (server) == 0)
        indicate_ready ();
}

static gboolean
vnc_data_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    gchar buffer[1024];
    gsize n_read;
    GIOStatus status;
    GError *error = NULL;

    status = g_io_channel_read_chars (channel, buffer, 1023, &n_read, &error);
    if (error)
        g_warning ("Error reading from VNC client: %s", error->message);
    g_clear_error (&error);

    if (status == G_IO_STATUS_NORMAL)
    {
        buffer[n_read] = '\0';
        if (g_str_has_suffix (buffer, "\n"))
            buffer[n_read-1] = '\0';
        status_notify ("XSERVER-%d VNC-CLIENT-CONNECT VERSION=\"%s\"", display_number, buffer);
    }
  
    return TRUE;
}

static void
request_cb (const gchar *request)
{
    if (!request)
    {
        g_main_loop_quit (loop);
        return;
    }
}

int
main (int argc, char **argv)
{
    char *pid_string;
    gboolean listen_tcp = TRUE;
    gboolean listen_unix = TRUE;
    gboolean use_inetd = FALSE;
    gchar *geometry = g_strdup ("640x480");
    gint depth = 8;
    gchar *lock_filename;
    int lock_file;
    int i;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);
    signal (SIGHUP, signal_cb);

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    loop = g_main_loop_new (NULL, FALSE);

    status_connect (request_cb);

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
                listen_tcp = FALSE;
            else if (strcmp (protocol, "unix") == 0)
                listen_unix = FALSE;
        }
        else if (strcmp (arg, "-geometry") == 0)
        {
            g_free (geometry);
            geometry = g_strdup (argv[i+1]);
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
  
    xserver = x_server_new (display_number);
    g_signal_connect (xserver, "client-connected", G_CALLBACK (client_connected_cb), NULL);
    g_signal_connect (xserver, "client-disconnected", G_CALLBACK (client_disconnected_cb), NULL);
    x_server_set_listen_unix (xserver, listen_unix);
    x_server_set_listen_tcp (xserver, listen_tcp);

    status_notify ("XSERVER-%d START GEOMETRY=%s DEPTH=%d", display_number, geometry, depth);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);
  
    if (use_inetd)
    {
        /* Send server protocol version to client */
        g_print ("RFB 003.007\n");

        if (!g_io_add_watch (g_io_channel_unix_new (STDIN_FILENO), G_IO_IN, vnc_data_cb, NULL))
            return EXIT_FAILURE;
    }
    else
    {
        g_printerr ("Only supported in -inetd mode\n");
        return EXIT_FAILURE;
    }

    lock_filename = g_strdup_printf (".X%d-lock", display_number);
    lock_path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "tmp", lock_filename, NULL);
    g_free (lock_filename);
    lock_file = open (lock_path, O_CREAT | O_EXCL | O_WRONLY, 0444);
    if (lock_file < 0)
    {
        fprintf (stderr,
                 "Fatal server error:\n"
                 "Server is already active for display %d\n"
                 "	If this server is no longer running, remove %s\n"
                 "	and start again.\n", display_number, lock_path);
        g_free (lock_path);
        lock_path = NULL;
        quit (EXIT_FAILURE);
    }
    pid_string = g_strdup_printf ("%10ld", (long) getpid ());
    if (write (lock_file, pid_string, strlen (pid_string)) < 0)
    {
        g_warning ("Error writing PID file: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }
    g_free (pid_string);

    if (!x_server_start (xserver))
        quit (EXIT_FAILURE);

    /* Indicate ready if parent process has requested it */
    indicate_ready ();

    g_main_loop_run (loop);

    cleanup ();

    return EXIT_SUCCESS;
}
