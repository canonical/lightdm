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
    cleanup ();
    exit (status);
}

static void
indicate_ready ()
{
    void *handler;  
    handler = signal (SIGUSR1, SIG_IGN);
    if (handler == SIG_IGN)
    {
        notify_status ("XSERVER :%d INDICATE-READY", display_number);
        kill (getppid (), SIGUSR1);
    }
    signal (SIGUSR1, handler);
}

static void
signal_cb (int signum)
{
    if (signum == SIGHUP)
    {
        notify_status ("XSERVER :%d DISCONNECT-CLIENTS", display_number);
        indicate_ready ();
    }
    else
    {
        notify_status ("XSERVER :%d TERMINATE SIGNAL=%d", display_number, signum);
        quit (EXIT_SUCCESS);
    }
}

static void
xdmcp_query_cb (XDMCPClient *client)
{
    static gboolean notified_query = FALSE;

    if (!notified_query)
    {
        notify_status ("XSERVER :%d SEND-QUERY", display_number);
        notified_query = TRUE;
    }
}

static void
xdmcp_willing_cb (XDMCPClient *client, XDMCPWilling *message)
{
    gchar **authorization_names;
    GInetAddress *addresses[2];

    notify_status ("XSERVER :%d GOT-WILLING AUTHENTICATION-NAME=\"%s\" HOSTNAME=\"%s\" STATUS=\"%s\"", display_number, message->authentication_name, message->hostname, message->status);

    notify_status ("XSERVER :%d SEND-REQUEST DISPLAY-NUMBER=%d AUTHORIZATION-NAME=\"%s\" MFID=\"%s\"", display_number, display_number, "MIT-MAGIC-COOKIE-1", "TEST XSERVER");

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
    notify_status ("XSERVER :%d GOT-ACCEPT SESSION-ID=%d AUTHENTICATION-NAME=\"%s\" AUTHORIZATION-NAME=\"%s\"", display_number, message->session_id, message->authentication_name, message->authorization_name);

    /* Ignore if haven't picked a valid authorization */
    if (strcmp (message->authorization_name, "MIT-MAGIC-COOKIE-1") != 0)
        return;

    g_free (xdmcp_cookie);
    xdmcp_cookie_length = message->authorization_data_length;
    xdmcp_cookie = g_malloc (message->authorization_data_length);
    memcpy (xdmcp_cookie, message->authorization_data, message->authorization_data_length);

    notify_status ("XSERVER :%d SEND-MANAGE SESSION-ID=%d DISPLAY-NUMBER=%d DISPLAY-CLASS=\"%s\"", display_number, message->session_id, display_number, "DISPLAY CLASS");
    xdmcp_client_send_manage (client, message->session_id, display_number, "DISPLAY CLASS");
}

static void
xdmcp_decline_cb (XDMCPClient *client, XDMCPDecline *message)
{
    notify_status ("XSERVER :%d GOT-DECLINE STATUS=\"%s\" AUTHENTICATION-NAME=\"%s\"", display_number, message->status, message->authentication_name);  
}

static void
xdmcp_failed_cb (XDMCPClient *client, XDMCPFailed *message)
{
    notify_status ("XSERVER :%d GOT-FAILED SESSION-ID=%d STATUS=\"%s\"", display_number, message->session_id, message->status);
}

static void
x_client_connect_cb (XClient *client, XConnect *message)
{
    gchar *auth_error = NULL;

    if (x_client_get_address (client))
        notify_status ("XSERVER :%d TCP-ACCEPT-CONNECT", display_number);
    else
        notify_status ("XSERVER :%d ACCEPT-CONNECT", display_number);

    if (xdmcp_client)
    {
        if (!xdmcp_cookie)
            auth_error = g_strdup ("Need to authenticate with XDMCP");
        else
        {
            gboolean matches = TRUE;
            if (message->authorization_protocol_data_length == xdmcp_cookie_length)
            {
                guint16 i;
                for (i = 0; i < xdmcp_cookie_length && message->authorization_protocol_data[i] == xdmcp_cookie[i]; i++);
                matches = i == xdmcp_cookie_length;
            }
            else
                matches = FALSE;

            if (strcmp (message->authorization_protocol_name, "MIT-MAGIC-COOKIE-1") != 0)
                auth_error = g_strdup ("Authorization required");
            else if (!matches)
                auth_error = g_strdup_printf ("Invalid MIT-MAGIC-COOKIE key");
        }
    }
    else if (auth_path)
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
x_client_intern_atom_cb (XClient *client, XInternAtom *message)
{
    if (strcmp (message->name, "SIGSEGV") == 0)
    {
        notify_status ("XSERVER :%d CRASH", display_number);
        cleanup ();
        kill (getpid (), SIGSEGV);
    }
}

static void
client_connected_cb (XServer *server, XClient *client)
{
    g_signal_connect (client, "connect", G_CALLBACK (x_client_connect_cb), NULL);
    g_signal_connect (client, "intern-atom", G_CALLBACK (x_client_intern_atom_cb), NULL);
}

static void
client_disconnected_cb (XServer *server, XClient *client)
{  
    g_signal_handlers_disconnect_matched (client, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, NULL);  
    if (x_server_get_n_clients (server) == 0)
        indicate_ready ();
}

int
main (int argc, char **argv)
{
    int i;
    char *pid_string, *return_lock;
    GMainLoop *loop;
    gboolean listen_tcp = TRUE;
    gboolean listen_unix = TRUE;
    gboolean do_xdmcp = FALSE;
    guint xdmcp_port = 0;
    gchar *xdmcp_host = NULL;
    int lock_file;
    FILE *f;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);
    signal (SIGHUP, signal_cb);
  
    g_type_init ();
  
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
        else if (g_str_has_prefix (arg, "-novtswitch"))
        {
            /* Ignore VT args */
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
                        "vtxx                   Use virtual terminal xx instead of the next available\n",
                        arg, argv[0]);
            return EXIT_FAILURE;
        }
    }

    xserver = x_server_new (display_number);
    g_signal_connect (xserver, "client-connected", G_CALLBACK (client_connected_cb), NULL);
    g_signal_connect (xserver, "client-disconnected", G_CALLBACK (client_disconnected_cb), NULL);
    x_server_set_listen_unix (xserver, listen_unix);
    x_server_set_listen_tcp (xserver, listen_tcp);

    notify_status ("XSERVER :%d START", display_number);

    config = g_key_file_new ();
    if (g_getenv ("LIGHTDM_TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    return_lock = g_build_filename (g_getenv ("LIGHTDM_TEST_HOME_DIR"), ".xserver-returned", NULL);
    f = fopen (return_lock, "r");
    if (f == NULL && g_key_file_has_key (config, "test-xserver-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-xserver-config", "return-value", NULL);
        notify_status ("XSERVER :%d EXIT CODE=%d", display_number, return_value);

        /* Write lock to stop repeatedly exiting */
        f = fopen (return_lock, "w");
        fclose (f);

        return return_value;
    }
    if (f != NULL)
        fclose (f);

    loop = g_main_loop_new (NULL, FALSE);

    lock_path = g_strdup_printf ("/tmp/.X%d-lock", display_number);
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

    return EXIT_SUCCESS;
}
