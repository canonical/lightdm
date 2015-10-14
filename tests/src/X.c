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

/* Version to pretend to be */
static gchar *xorg_version;
static gint xorg_version_major, xorg_version_minor;

/* Path to lock file */
static gchar *lock_path = NULL;

/* TRUE if we allow TCP connections */
static gboolean listen_tcp = TRUE;

/* TRUE if we allow Unix connections */
static gboolean listen_unix = TRUE;

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

/* XDMCP client */
static XDMCPClient *xdmcp_client = NULL;

/* Authorization provided by XDMCP server */
static guint16 xdmcp_cookie_length = 0;
static guint8 *xdmcp_cookie = NULL;

static void
cleanup (void)
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
xdmcp_query_cb (XDMCPClient *client)
{
    static gboolean notified_query = FALSE;

    if (!notified_query)
    {
        status_notify ("%s SEND-QUERY", id);
        notified_query = TRUE;
    }
}

static void
xdmcp_willing_cb (XDMCPClient *client, XDMCPWilling *message)
{
    gchar **authorization_names;
    GInetAddress *addresses[2];

    status_notify ("%s GOT-WILLING AUTHENTICATION-NAME=\"%s\" HOSTNAME=\"%s\" STATUS=\"%s\"", id, message->authentication_name, message->hostname, message->status);

    status_notify ("%s SEND-REQUEST DISPLAY-NUMBER=%d AUTHORIZATION-NAME=\"%s\" MFID=\"%s\"", id, display_number, "MIT-MAGIC-COOKIE-1", "TEST XSERVER");

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
    status_notify ("%s GOT-ACCEPT SESSION-ID=%d AUTHENTICATION-NAME=\"%s\" AUTHORIZATION-NAME=\"%s\"", id, message->session_id, message->authentication_name, message->authorization_name);

    /* Ignore if haven't picked a valid authorization */
    if (strcmp (message->authorization_name, "MIT-MAGIC-COOKIE-1") != 0)
        return;

    g_free (xdmcp_cookie);
    xdmcp_cookie_length = message->authorization_data_length;
    xdmcp_cookie = g_malloc (message->authorization_data_length);
    memcpy (xdmcp_cookie, message->authorization_data, message->authorization_data_length);

    status_notify ("%s SEND-MANAGE SESSION-ID=%d DISPLAY-NUMBER=%d DISPLAY-CLASS=\"%s\"", id, message->session_id, display_number, "DISPLAY CLASS");
    xdmcp_client_send_manage (client, message->session_id, display_number, "DISPLAY CLASS");
}

static void
xdmcp_decline_cb (XDMCPClient *client, XDMCPDecline *message)
{
    status_notify ("%s GOT-DECLINE STATUS=\"%s\" AUTHENTICATION-NAME=\"%s\"", id, message->status, message->authentication_name);
}

static void
xdmcp_failed_cb (XDMCPClient *client, XDMCPFailed *message)
{
    status_notify ("%s GOT-FAILED SESSION-ID=%d STATUS=\"%s\"", id, message->session_id, message->status);
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

    if (strcmp (name, "CRASH") == 0)
    {
        cleanup ();
        kill (getpid (), SIGSEGV);
    }

    else if (strcmp (name, "INDICATE-READY") == 0)
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

    else if (strcmp (name, "START-XDMCP") == 0)
    {
        if (!xdmcp_client_start (xdmcp_client))
            quit (EXIT_FAILURE);
    }
}

static int
version_compare (int major, int minor)
{
    if (major == xorg_version_major)
        return xorg_version_minor - minor;
    else
        return xorg_version_major - major;
}

int
main (int argc, char **argv)
{
    int i;
    gchar **tokens;
    char *pid_string;
    gboolean do_xdmcp = FALSE;
    guint xdmcp_port = 0;
    gchar *xdmcp_host = NULL;
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

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    xorg_version = g_key_file_get_string (config, "test-xserver-config", "version", NULL);
    if (!xorg_version)
        xorg_version = g_strdup ("1.16.0");
    tokens = g_strsplit (xorg_version, ".", -1);
    xorg_version_major = g_strv_length (tokens) > 0 ? atoi (tokens[0]) : 0;
    xorg_version_minor = g_strv_length (tokens) > 1 ? atoi (tokens[1]) : 0;
    g_strfreev (tokens);

    /* TCP listening default changed in 1.17.0 */
    listen_tcp = version_compare (1, 17) < 0;

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
        else if (strcmp (arg, "-listen") == 0 && version_compare (1, 17) >= 0)
        {
            char *protocol = argv[i+1];
            i++;
            if (strcmp (protocol, "tcp") == 0)
                listen_tcp = TRUE;
            else if (strcmp (protocol, "unix") == 0)
                listen_unix = TRUE;
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
            listen_tcp = TRUE;
            i++;
        }
        else if (strcmp (arg, "-broadcast") == 0)
        {
            do_xdmcp = TRUE;
            listen_tcp = TRUE;
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
        else if (strcmp (arg, "-version") == 0)
        {
            fprintf (stderr, "\nX.Org X Server %s\nBlah blah blah\n", xorg_version);
            return EXIT_SUCCESS;
        }
        else
        {
            g_printerr ("Unrecognized option: %s\n"
                        "Use: %s [:<display>] [option]\n"
                        "-auth file             Select authorization file\n"
                        "-nolisten protocol     Don't listen on protocol\n"
                        "-listen protocol       Listen on protocol\n"
                        "-background [none]     Create root window with no background\n"
                        "-nr                    (Ubuntu-specific) Synonym for -background none\n"
                        "-query host-name       Contact named host for XDMCP\n"
                        "-broadcast             Broadcast for XDMCP\n"
                        "-port port-num         UDP port number to send messages to\n"
                        "-seat string           seat to run on\n"
                        "-sharevts              share VTs with another X server\n"
                        "-mir id                Mir ID to use\n"
                        "-mirSocket name        Mir socket to use\n"
                        "-version               show the server version\n"
                        "vtxx                   Use virtual terminal xx instead of the next available\n",
                        arg, argv[0]);
            return EXIT_FAILURE;
        }
    }

    id = g_strdup_printf ("XSERVER-%d", display_number);

    status_connect (request_cb, id);

    xserver = x_server_new (display_number);
    g_signal_connect (xserver, X_SERVER_SIGNAL_CLIENT_CONNECTED, G_CALLBACK (client_connected_cb), NULL);
    g_signal_connect (xserver, X_SERVER_SIGNAL_CLIENT_DISCONNECTED, G_CALLBACK (client_disconnected_cb), NULL);

    status_text = g_string_new ("");
    g_string_printf (status_text, "%s START", id);
    if (vt_number >= 0)
        g_string_append_printf (status_text, " VT=%d", vt_number);
    if (listen_tcp)
        g_string_append (status_text, " LISTEN-TCP");
    if (!listen_unix)
        g_string_append (status_text, " NO-LISTEN-UNIX");
    if (seat != NULL)
        g_string_append_printf (status_text, " SEAT=%s", seat);
    if (sharevts)
        g_string_append (status_text, " SHAREVTS=TRUE");
    if (mir_id != NULL)
        g_string_append_printf (status_text, " MIR-ID=%s", mir_id);
    status_notify ("%s", status_text->str);
    g_string_free (status_text, TRUE);

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

    /* Enable XDMCP */
    if (do_xdmcp)
    {
        xdmcp_client = xdmcp_client_new ();
        if (xdmcp_host > 0)
            xdmcp_client_set_hostname (xdmcp_client, xdmcp_host);
        if (xdmcp_port > 0)
            xdmcp_client_set_port (xdmcp_client, xdmcp_port);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_QUERY, G_CALLBACK (xdmcp_query_cb), NULL);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_WILLING, G_CALLBACK (xdmcp_willing_cb), NULL);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_ACCEPT, G_CALLBACK (xdmcp_accept_cb), NULL);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_DECLINE, G_CALLBACK (xdmcp_decline_cb), NULL);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_FAILED, G_CALLBACK (xdmcp_failed_cb), NULL);
    }

    g_main_loop_run (loop);

    cleanup ();

    return exit_status;
}
