#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

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

/* Display number being served */
static int display_number = 0;

/* X server */
static XServer *xserver = NULL;

/* XDMCP client */
static XDMCPClient *xdmcp_client = NULL;

/* Authorization provided by XDMCP server */
static guint16 xdmcp_cookie_length = 0;
static guint8 *xdmcp_cookie = NULL;

static void
cleanup ()
{
    if (lock_path)
        unlink (lock_path);
    if (xserver)
        g_object_unref (xserver);
    if (xdmcp_client)
        g_object_unref (xdmcp_client);
}

static void
quit (int status)
{
    exit_status = status;
    g_main_loop_quit (loop);
}

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
xdmcp_query_cb (XDMCPClient *client)
{
    static gboolean notified_query = FALSE;

    if (!notified_query)
    {
        status_notify ("XSERVER-%d SEND-QUERY", display_number);
        notified_query = TRUE;
    }
}

static void
xdmcp_willing_cb (XDMCPClient *client, XDMCPWilling *message)
{
    gchar **authorization_names;
    GInetAddress *addresses[2];

    status_notify ("XSERVER-%d GOT-WILLING AUTHENTICATION-NAME=\"%s\" HOSTNAME=\"%s\" STATUS=\"%s\"", display_number, message->authentication_name, message->hostname, message->status);

    status_notify ("XSERVER-%d SEND-REQUEST DISPLAY-NUMBER=%d AUTHORIZATION-NAME=\"%s\" MFID=\"%s\"", display_number, display_number, "MIT-MAGIC-COOKIE-1", "TEST XSERVER");

    authorization_names = g_strsplit ("MIT-MAGIC-COOKIE-1", " ", -1);
    addresses[0] = xdmcp_client_get_local_address (client);
    addresses[1] = NULL;
    xdmcp_client_send_request (client, display_number,
                               addresses,
                               "", NULL, 0,
                               authorization_names, "TEST XSERVER");
    g_strfreev (authorization_names);
}

static void
xdmcp_accept_cb (XDMCPClient *client, XDMCPAccept *message)
{
    status_notify ("XSERVER-%d GOT-ACCEPT SESSION-ID=%d AUTHENTICATION-NAME=\"%s\" AUTHORIZATION-NAME=\"%s\"", display_number, message->session_id, message->authentication_name, message->authorization_name);

    /* Ignore if haven't picked a valid authorization */
    if (strcmp (message->authorization_name, "MIT-MAGIC-COOKIE-1") != 0)
        return;

    g_free (xdmcp_cookie);
    xdmcp_cookie_length = message->authorization_data_length;
    xdmcp_cookie = g_malloc (message->authorization_data_length);
    memcpy (xdmcp_cookie, message->authorization_data, message->authorization_data_length);

    status_notify ("XSERVER-%d SEND-MANAGE SESSION-ID=%d DISPLAY-NUMBER=%d DISPLAY-CLASS=\"%s\"", display_number, message->session_id, display_number, "DISPLAY CLASS");
    xdmcp_client_send_manage (client, message->session_id, display_number, "DISPLAY CLASS");
}

static void
xdmcp_decline_cb (XDMCPClient *client, XDMCPDecline *message)
{
    status_notify ("XSERVER-%d GOT-DECLINE STATUS=\"%s\" AUTHENTICATION-NAME=\"%s\"", display_number, message->status, message->authentication_name);  
}

static void
xdmcp_failed_cb (XDMCPClient *client, XDMCPFailed *message)
{
    status_notify ("XSERVER-%d GOT-FAILED SESSION-ID=%d STATUS=\"%s\"", display_number, message->session_id, message->status);
}

static void
client_connected_cb (XServer *server, XClient *client)
{
    gchar *auth_error = NULL;

    status_notify ("XSERVER-%d ACCEPT-CONNECT", display_number);

    if (auth_error)
        x_client_send_failed (client, auth_error);
    else
        x_client_send_success (client);
    g_free (auth_error);
}

static void
client_disconnected_cb (XServer *server, XClient *client)
{  
    g_signal_handlers_disconnect_matched (client, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, NULL);  
    if (x_server_get_n_clients (server) == 0)
        indicate_ready ();
}

static void
request_cb (const gchar *request)
{
    gchar *r;
  
    if (!request)
    {
        g_main_loop_quit (loop);
        return;
    }

    r = g_strdup_printf ("XSERVER-%d CRASH", display_number);
    if (strcmp (request, r) == 0)
    {
        cleanup ();
        kill (getpid (), SIGSEGV);
    }
    g_free (r);
}

int
main (int argc, char **argv)
{
    int i;
    char *pid_string;
    gboolean listen_tcp = TRUE;
    gboolean listen_unix = TRUE;
    gboolean do_xdmcp = FALSE;
    guint xdmcp_port = 0;
    gchar *xdmcp_host = NULL;
    gchar *lock_filename;
    int lock_file;

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
        else if (strcmp (arg, "-nr") == 0)
        {
        }
        else if (strcmp (arg, "-background") == 0)
        {
            /* Ignore arg */
            i++;
        }
        else if (strcmp (arg, "-port") == 0)
        {
            xdmcp_port = atoi (argv[i+1]);
            i++;
        }
        else if (strcmp (arg, "-query") == 0)
        {
            do_xdmcp = TRUE;
            xdmcp_host = argv[i+1];
            i++;
        }
        else if (strcmp (arg, "-broadcast") == 0)
        {
            do_xdmcp = TRUE;
        }
        else if (g_str_has_prefix (arg, "vt"))
        {
            /* Ignore VT args */
        }
        else if (strcmp (arg, "-novtswitch") == 0)
        {
            /* Ignore VT args */
        }
        else if (strcmp (arg, "-mir") == 0)
        {
            /* FIXME */
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
                        "-query host-name       Contact named host for XDMCP\n"
                        "-broadcast             Broadcast for XDMCP\n"
                        "-port port-num         UDP port number to send messages to\n"
                        "-mir id                Mir ID to use\n"
                        "-mirSocket name        Mir socket to use\n"
                        "vtxx                   Use virtual terminal xx instead of the next available\n",
                        arg, argv[0]);
            return EXIT_FAILURE;
        }
    }

    xserver = x_server_new (display_number);
    g_signal_connect (xserver, "client-connected", G_CALLBACK (client_connected_cb), NULL);
    g_signal_connect (xserver, "client-disconnected", G_CALLBACK (client_disconnected_cb), NULL);

    status_notify ("XSERVER-%d START", display_number);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    if (g_key_file_has_key (config, "test-xserver-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-xserver-config", "return-value", NULL);
        status_notify ("XSERVER-%d EXIT CODE=%d", display_number, return_value);
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

    /* Enable XDMCP */
    if (do_xdmcp)
    {
        xdmcp_client = xdmcp_client_new ();
        if (xdmcp_host > 0)
            xdmcp_client_set_hostname (xdmcp_client, xdmcp_host);
        if (xdmcp_port > 0)
            xdmcp_client_set_port (xdmcp_client, xdmcp_port);
        g_signal_connect (xdmcp_client, "query", G_CALLBACK (xdmcp_query_cb), NULL);      
        g_signal_connect (xdmcp_client, "willing", G_CALLBACK (xdmcp_willing_cb), NULL);
        g_signal_connect (xdmcp_client, "accept", G_CALLBACK (xdmcp_accept_cb), NULL);
        g_signal_connect (xdmcp_client, "decline", G_CALLBACK (xdmcp_decline_cb), NULL);
        g_signal_connect (xdmcp_client, "failed", G_CALLBACK (xdmcp_failed_cb), NULL);

        if (!xdmcp_client_start (xdmcp_client))
            quit (EXIT_FAILURE);
    }

    /* Indicate ready if parent process has requested it */
    indicate_ready ();

    g_main_loop_run (loop);

    cleanup ();

    return exit_status;
}
