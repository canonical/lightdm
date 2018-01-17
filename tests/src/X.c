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

/* Configuration to use */
static gchar *config_file = NULL;

/* Configuration layout to use */
static gchar *layout = NULL;

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

/* Session ID provided by XDMCP server */
static guint32 xdmcp_session_id = 0;

/* Authorization provided by XDMCP server */
static guint16 xdmcp_cookie_length = 0;
static guint8 *xdmcp_cookie = NULL;

/* Terminate on server reset */
static gboolean terminate_on_reset = FALSE;

static void
cleanup (void)
{
    if (lock_path)
        unlink (lock_path);
    g_clear_object (&xserver);
    g_clear_object (&xdmcp_client);
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
xdmcp_willing_cb (XDMCPClient *client, XDMCPWilling *message)
{
    status_notify ("%s GOT-WILLING AUTHENTICATION-NAME=\"%s\" HOSTNAME=\"%s\" STATUS=\"%s\"", id, message->authentication_name, message->hostname, message->status);
}

static void
xdmcp_unwilling_cb (XDMCPClient *client, XDMCPUnwilling *message)
{
    status_notify ("%s GOT-UNWILLING HOSTNAME=\"%s\" STATUS=\"%s\"", id, message->hostname, message->status);
}

static gchar *
data_to_string (guint8 *data, gsize data_length)
{
    gchar *text = malloc (data_length * 2 + 1);
    gsize i;
    for (i = 0; i < data_length; i++)
    {
        static gchar hex_char[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
        text[i*2] = hex_char [data[i] >> 4];
        text[i*2 + 1] = hex_char [data[i] & 0xF];
    }
    text[i*2] = '\0';

    return text;
}

static void
xdmcp_accept_cb (XDMCPClient *client, XDMCPAccept *message)
{
    g_autofree gchar *authentication_data_text = data_to_string (message->authentication_data, message->authentication_data_length);
    g_autofree gchar *authorization_data_text = data_to_string (message->authorization_data, message->authorization_data_length);
    status_notify ("%s GOT-ACCEPT SESSION-ID=%d AUTHENTICATION-NAME=\"%s\" AUTHENTICATION-DATA=%s AUTHORIZATION-NAME=\"%s\" AUTHORIZATION-DATA=%s",
                   id, message->session_id, message->authentication_name, authentication_data_text, message->authorization_name, authorization_data_text);

    xdmcp_session_id = message->session_id;

    g_free (xdmcp_cookie);
    xdmcp_cookie_length = message->authorization_data_length;
    xdmcp_cookie = g_malloc (message->authorization_data_length);
    memcpy (xdmcp_cookie, message->authorization_data, message->authorization_data_length);
}

static void
xdmcp_decline_cb (XDMCPClient *client, XDMCPDecline *message)
{
    g_autofree gchar *authentication_data_text = data_to_string (message->authentication_data, message->authentication_data_length);
    status_notify ("%s GOT-DECLINE STATUS=\"%s\" AUTHENTICATION-NAME=\"%s\" AUTHENTICATION-DATA=%s", id, message->status, message->authentication_name, authentication_data_text);
}

static void
xdmcp_failed_cb (XDMCPClient *client, XDMCPFailed *message)
{
    status_notify ("%s GOT-FAILED SESSION-ID=%d STATUS=\"%s\"", id, message->session_id, message->status);
}

static void
xdmcp_alive_cb (XDMCPClient *client, XDMCPAlive *message)
{
    status_notify ("%s GOT-ALIVE SESSION-RUNNING=%s SESSION-ID=%d", id, message->session_running ? "TRUE" : "FALSE", message->session_id);
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
reset_cb (XServer *server)
{
    if (terminate_on_reset)
    {
        status_notify ("%s TERMINATE", id);
        quit (EXIT_SUCCESS);
    }
}

static guint8
get_nibble (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else
        return 0;
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
        void *handler = signal (SIGUSR1, SIG_IGN);
        if (handler == SIG_IGN)
        {
            status_notify ("%s INDICATE-READY", id);
            kill (getppid (), SIGUSR1);
        }
        signal (SIGUSR1, handler);
    }

    else if (strcmp (name, "SEND-QUERY") == 0)
    {
        if (!xdmcp_client_start (xdmcp_client))
            quit (EXIT_FAILURE);

        const gchar *authentication_names_list = g_hash_table_lookup (params, "AUTHENTICATION-NAMES");
        if (!authentication_names_list)
            authentication_names_list = "";
        g_auto(GStrv) authentication_names = g_strsplit (authentication_names_list, " ", -1);

        xdmcp_client_send_query (xdmcp_client, authentication_names);
    }

    else if (strcmp (name, "SEND-REQUEST") == 0)
    {
        int request_display_number = display_number;
        const gchar *text = g_hash_table_lookup (params, "DISPLAY-NUMBER");
        if (text)
            request_display_number = atoi (text);

        const gchar *addresses_list = g_hash_table_lookup (params, "ADDRESSES");
        if (!addresses_list)
            addresses_list = "";

        const gchar *authentication_name = g_hash_table_lookup (params, "AUTHENTICATION-NAME");
        if (!authentication_name)
            authentication_name = "";

        const gchar *authentication_data_text = g_hash_table_lookup (params, "AUTHENTICATION-DATA");
        if (!authentication_data_text)
            authentication_data_text = "";

        const gchar *authorization_names_list = g_hash_table_lookup (params, "AUTHORIZATION-NAMES");
        if (!authorization_names_list)
            authorization_names_list = "";

        const gchar *mfid = g_hash_table_lookup (params, "MFID");
        if (!mfid)
            mfid = "";

        g_auto(GStrv) list = g_strsplit (addresses_list, " ", -1);
        gsize list_length = g_strv_length (list);
        GInetAddress **addresses = g_malloc (sizeof (GInetAddress *) * (list_length + 1));
        gsize i;
        for (i = 0; i < list_length; i++)
            addresses[i] = g_inet_address_new_from_string (list[i]);
        addresses[i] = NULL;

        gsize authentication_data_length = strlen (authentication_data_text) / 2;
        g_autofree guint8 *authentication_data = malloc (authentication_data_length);
        for (gsize i = 0; i < authentication_data_length; i++)
            authentication_data[i] = get_nibble (authentication_data_text[i*2]) << 4 | get_nibble (authentication_data_text[i*2+1]);

        g_auto(GStrv) authorization_names = g_strsplit (authorization_names_list, " ", -1);

        xdmcp_client_send_request (xdmcp_client,
                                   request_display_number,
                                   addresses,
                                   authentication_name,
                                   authentication_data, authentication_data_length,
                                   authorization_names, mfid);
        for (gsize i = 0; addresses[i] != NULL; i++)
            g_object_unref (addresses[i]);
        g_free (addresses);
    }

    else if (strcmp (name, "SEND-MANAGE") == 0)
    {
        guint32 session_id = xdmcp_session_id;
        const gchar *text = g_hash_table_lookup (params, "SESSION-ID");
        if (text)
            session_id = atoi (text);

        guint16 manage_display_number = display_number;
        text = g_hash_table_lookup (params, "DISPLAY-NUMBER");
        if (text)
            manage_display_number = atoi (text);

        const gchar *display_class = g_hash_table_lookup (params, "DISPLAY-CLASS");
        if (!display_class)
            display_class = "";

        xdmcp_client_send_manage (xdmcp_client,
                                  session_id,
                                  manage_display_number,
                                  display_class);
    }

    else if (strcmp (name, "SEND-KEEP-ALIVE") == 0)
    {
        guint16 keep_alive_display_number = display_number;
        const gchar *text = g_hash_table_lookup (params, "DISPLAY-NUMBER");
        if (text)
            keep_alive_display_number = atoi (text);

        guint32 session_id = xdmcp_session_id;
        text = g_hash_table_lookup (params, "SESSION-ID");
        if (text)
            session_id = atoi (text);

        xdmcp_client_send_keep_alive (xdmcp_client, keep_alive_display_number, session_id);
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
        xorg_version = g_strdup ("1.17.0");
    g_auto(GStrv) tokens = g_strsplit (xorg_version, ".", -1);
    xorg_version_major = g_strv_length (tokens) > 0 ? atoi (tokens[0]) : 0;
    xorg_version_minor = g_strv_length (tokens) > 1 ? atoi (tokens[1]) : 0;

    /* TCP listening default changed in 1.17.0 */
    listen_tcp = version_compare (1, 17) < 0;

    gboolean do_xdmcp = FALSE;
    guint xdmcp_port = 0;
    const gchar *xdmcp_host = NULL;
    const gchar *seat = NULL;
    const gchar *mir_id = NULL;
    for (int i = 1; i < argc; i++)
    {
        char *arg = argv[i];

        if (arg[0] == ':')
        {
            display_number = atoi (arg + 1);
        }
        else if (strcmp (arg, "-config") == 0)
        {
            config_file = argv[i+1];
            i++;
        }
        else if (strcmp (arg, "-layout") == 0)
        {
            layout = argv[i+1];
            i++;
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
        else if (strcmp (arg, "-terminate") == 0)
        {
            terminate_on_reset = TRUE;
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
                        "-config file           Specify a configuration file\n"
                        "-layout name           Specify the ServerLayout section name\n"
                        "-auth file             Select authorization file\n"
                        "-nolisten protocol     Don't listen on protocol\n"
                        "-listen protocol       Listen on protocol\n"
                        "-background [none]     Create root window with no background\n"
                        "-nr                    (Ubuntu-specific) Synonym for -background none\n"
                        "-query host-name       Contact named host for XDMCP\n"
                        "-broadcast             Broadcast for XDMCP\n"
                        "-port port-num         UDP port number to send messages to\n"
                        "-seat string           seat to run on\n"
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
    g_signal_connect (xserver, X_SERVER_SIGNAL_RESET, G_CALLBACK (reset_cb), NULL);

    g_autoptr(GString) status_text = g_string_new ("");
    g_string_printf (status_text, "%s START", id);
    if (config_file)
        g_string_append_printf (status_text, " CONFIG=%s", config_file);
    if (layout)
        g_string_append_printf (status_text, " LAYOUT=%s", layout);
    if (vt_number >= 0)
        g_string_append_printf (status_text, " VT=%d", vt_number);
    if (listen_tcp)
        g_string_append (status_text, " LISTEN-TCP");
    if (!listen_unix)
        g_string_append (status_text, " NO-LISTEN-UNIX");
    if (seat != NULL)
        g_string_append_printf (status_text, " SEAT=%s", seat);
    if (mir_id != NULL)
        g_string_append_printf (status_text, " MIR-ID=%s", mir_id);
    status_notify ("%s", status_text->str);

    if (g_key_file_has_key (config, "test-xserver-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-xserver-config", "return-value", NULL);
        status_notify ("%s EXIT CODE=%d", id, return_value);
        return return_value;
    }

    g_autofree gchar *lock_filename = g_strdup_printf (".X%d-lock", display_number);
    lock_path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "tmp", lock_filename, NULL);
    int lock_file = open (lock_path, O_CREAT | O_EXCL | O_WRONLY, 0444);
    if (lock_file < 0)
    {
        g_autofree gchar *lock_contents = NULL;

        if (g_file_get_contents (lock_path, &lock_contents, NULL, NULL))
        {
            g_autofree gchar *proc_filename = NULL;
            pid_t pid;

            pid = atol (lock_contents);

            proc_filename = g_strdup_printf ("/proc/%d", pid);
            if (!g_file_test (proc_filename, G_FILE_TEST_EXISTS))
            {
                g_autofree gchar *socket_dir = NULL;
                g_autofree gchar *socket_filename = NULL;
                g_autofree gchar *socket_path = NULL;

                socket_dir = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "tmp", ".X11-unix", NULL);
                g_mkdir_with_parents (socket_dir, 0755);

                socket_filename = g_strdup_printf ("X%d", display_number);
                socket_path = g_build_filename (socket_dir, socket_filename, NULL);

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

    /* Enable XDMCP */
    if (do_xdmcp)
    {
        xdmcp_client = xdmcp_client_new ();
        if (xdmcp_host > 0)
            xdmcp_client_set_hostname (xdmcp_client, xdmcp_host);
        if (xdmcp_port > 0)
            xdmcp_client_set_port (xdmcp_client, xdmcp_port);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_WILLING, G_CALLBACK (xdmcp_willing_cb), NULL);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_UNWILLING, G_CALLBACK (xdmcp_unwilling_cb), NULL);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_ACCEPT, G_CALLBACK (xdmcp_accept_cb), NULL);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_DECLINE, G_CALLBACK (xdmcp_decline_cb), NULL);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_FAILED, G_CALLBACK (xdmcp_failed_cb), NULL);
        g_signal_connect (xdmcp_client, XDMCP_CLIENT_SIGNAL_ALIVE, G_CALLBACK (xdmcp_alive_cb), NULL);
    }

    g_main_loop_run (loop);

    cleanup ();

    return exit_status;
}
