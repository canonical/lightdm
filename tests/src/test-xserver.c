#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <glib.h>

#include "status.h"

/* For some reason sys/un.h doesn't define this */
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

static gchar *socket_path = NULL;
static gchar *lock_path = NULL;
static gchar *auth_path = NULL;

static int display_number = 0;

#define BYTE_ORDER_MSB 'B'
#define BYTE_ORDER_LSB 'l'

#define PROTOCOL_MAJOR_VERSION 11
#define PROTOCOL_MINOR_VERSION 0

#define RELEASE_NUMBER 0
#define RESOURCE_ID_BASE 0x04e00000
#define RESOURCE_ID_MASK 0x001fffff
#define MOTION_BUFFER_SIZE 256
#define MAXIMUM_REQUEST_LENGTH 65535
#define BITMAP_FORMAT_SCANLINE_UNIT 32
#define BITMAP_FORMAT_SCANLINE_PAD 32
#define MIN_KEYCODE 8
#define MAX_KEYCODE 255
#define VENDOR "LightDM"

enum
{
    Failed = 0,
    Success = 1,
    Authenticate = 2
};

static gsize
pad (gsize length)
{
    if (length % 4 == 0)
        return 0;
    return 4 - length % 4;
}

static void
read_padding (gsize length, gsize *offset)
{
    *offset += length;
}

static guint8
read_card8 (guint8 *buffer, gsize buffer_length, gsize *offset)
{
    if (*offset >= buffer_length)
        return 0;
    (*offset)++;
    return buffer[*offset - 1];
}

static guint16
read_card16 (guint8 *buffer, gsize buffer_length, guint8 byte_order, gsize *offset)
{
    guint8 a, b;

    a = read_card8 (buffer, buffer_length, offset);
    b = read_card8 (buffer, buffer_length, offset);
    if (byte_order == BYTE_ORDER_MSB)
        return a << 8 | b;
    else
        return b << 8 | a;
}

static guint8 *
read_string8 (guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset)
{
    guint8 *string;
    int i;

    string = g_malloc (string_length + 1);
    for (i = 0; i < string_length; i++)
        string[i] = read_card8 (buffer, buffer_length, offset);
    string[i] = '\0';
    return string;
}

static gchar *
read_padded_string (guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset)
{
    guint8 *value;
    value = read_string8 (buffer, buffer_length, string_length, offset);
    read_padding (pad (string_length), offset);
    return (gchar *) value;
}

static void
write_card8 (guint8 *buffer, gsize buffer_length, guint8 value, gsize *offset)
{
    if (*offset >= buffer_length)
        return;
    buffer[*offset] = value;
    (*offset)++;
}

static void
write_padding (guint8 *buffer, gsize buffer_length, gsize length, gsize *offset)
{
    gsize i;
    for (i = 0; i < length; i++)
        write_card8 (buffer, buffer_length, 0, offset);
}

static void
write_card16 (guint8 *buffer, gsize buffer_length, guint8 byte_order, guint16 value, gsize *offset)
{
    if (byte_order == BYTE_ORDER_MSB)
    {
        write_card8 (buffer, buffer_length, value >> 8, offset);
        write_card8 (buffer, buffer_length, value & 0xFF, offset);
    }
    else
    {
        write_card8 (buffer, buffer_length, value & 0xFF, offset);
        write_card8 (buffer, buffer_length, value >> 8, offset);
    }
}

static void
write_card32 (guint8 *buffer, gsize buffer_length, guint8 byte_order, guint32 value, gsize *offset)
{
    if (byte_order == BYTE_ORDER_MSB)
    {
        write_card8 (buffer, buffer_length, value >> 24, offset);
        write_card8 (buffer, buffer_length, (value >> 16) & 0xFF, offset);
        write_card8 (buffer, buffer_length, (value >> 8) & 0xFF, offset);
        write_card8 (buffer, buffer_length, value & 0xFF, offset);
    }
    else
    {
        write_card8 (buffer, buffer_length, value & 0xFF, offset);
        write_card8 (buffer, buffer_length, (value >> 8) & 0xFF, offset);
        write_card8 (buffer, buffer_length, (value >> 16) & 0xFF, offset);
        write_card8 (buffer, buffer_length, value >> 24, offset);
    }
}

static void
write_string8 (guint8 *buffer, gsize buffer_length, const guint8 *value, gsize value_length, gsize *offset)
{
    gsize i;
    for (i = 0; i < value_length; i++)
        write_card8 (buffer, buffer_length, value[i], offset);
}

static gsize
padded_string_length (const gchar *value)
{
    return strlen (value) + pad (strlen (value)) / 4;
}

static void
write_padded_string (guint8 *buffer, gsize buffer_length, const gchar *value, gsize *offset)
{
    write_string8 (buffer, buffer_length, (guint8 *) value, strlen (value), offset);
    write_padding (buffer, buffer_length, pad (strlen (value)), offset);
}

static void
decode_connect (guint8 *buffer, gsize buffer_length,
                guint8 *byte_order,
                guint16 *protocol_major_version, guint16 *protocol_minor_version,
                gchar **authorization_protocol_name,
                guint8 **authorization_protocol_data, guint16 *authorization_protocol_data_length)
{
    gsize offset = 0;
    guint16 n;

    *byte_order = read_card8 (buffer, buffer_length, &offset);
    read_padding (1, &offset);
    *protocol_major_version = read_card16 (buffer, buffer_length, *byte_order, &offset);
    *protocol_minor_version = read_card16 (buffer, buffer_length, *byte_order, &offset);
    n = read_card16 (buffer, buffer_length, *byte_order, &offset);
    *authorization_protocol_data_length = read_card16 (buffer, buffer_length, *byte_order, &offset);
    read_padding (2, &offset);
    *authorization_protocol_name = read_padded_string (buffer, buffer_length, n, &offset);
    *authorization_protocol_data = read_string8 (buffer, buffer_length, *authorization_protocol_data_length, &offset);
    read_padding (pad (*authorization_protocol_data_length), &offset);
}

static gsize
encode_failed (guint8 *buffer, gsize buffer_length,
               guint8 byte_order, const gchar *reason)
{
    gsize offset = 0;
    guint8 additional_data_length;

    write_card8 (buffer, buffer_length, Failed, &offset);
    write_card8 (buffer, buffer_length, strlen (reason), &offset);
    write_card16 (buffer, buffer_length, byte_order, PROTOCOL_MAJOR_VERSION, &offset);
    write_card16 (buffer, buffer_length, byte_order, PROTOCOL_MINOR_VERSION, &offset);
    additional_data_length = padded_string_length (reason);
    write_card16 (buffer, buffer_length, byte_order, additional_data_length, &offset);

    /* Additional data */
    write_padded_string (buffer, buffer_length, reason, &offset);

    return offset;
}

static gsize
encode_accept (guint8 *buffer, gsize buffer_length,
               guint8 byte_order)
{
    gsize offset = 0;
    guint8 additional_data_length;

    write_card8 (buffer, buffer_length, Success, &offset);
    write_padding (buffer, buffer_length, 1, &offset);
    write_card16 (buffer, buffer_length, byte_order, PROTOCOL_MAJOR_VERSION, &offset);
    write_card16 (buffer, buffer_length, byte_order, PROTOCOL_MINOR_VERSION, &offset);
    additional_data_length = 8 + padded_string_length (VENDOR);
    write_card16 (buffer, buffer_length, byte_order, additional_data_length, &offset);

    /* Additional data */
    write_card32 (buffer, buffer_length, byte_order, RELEASE_NUMBER, &offset);
    write_card32 (buffer, buffer_length, byte_order, RESOURCE_ID_BASE, &offset);
    write_card32 (buffer, buffer_length, byte_order, RESOURCE_ID_MASK, &offset);
    write_card32 (buffer, buffer_length, byte_order, MOTION_BUFFER_SIZE, &offset);
    write_card16 (buffer, buffer_length, byte_order, strlen (VENDOR), &offset);
    write_card16 (buffer, buffer_length, byte_order, MAXIMUM_REQUEST_LENGTH, &offset);
    write_card8 (buffer, buffer_length, 0, &offset); // number of screens
    write_card8 (buffer, buffer_length, 0, &offset); // number of pixmap formats
    write_card8 (buffer, buffer_length, 0, &offset); // image-byte-order
    write_card8 (buffer, buffer_length, 0, &offset); // bitmap-format-bit-order
    write_card8 (buffer, buffer_length, BITMAP_FORMAT_SCANLINE_UNIT, &offset);
    write_card8 (buffer, buffer_length, BITMAP_FORMAT_SCANLINE_PAD, &offset);
    write_card8 (buffer, buffer_length, MIN_KEYCODE, &offset);
    write_card8 (buffer, buffer_length, MAX_KEYCODE, &offset);
    write_padding (buffer, buffer_length, 4, &offset);
    write_padded_string (buffer, buffer_length, VENDOR, &offset);
    // pixmap formats
    // screens

    return offset;
}

static void
log_buffer (const gchar *text, const guint8 *buffer, gsize buffer_length)
{
    gsize i;

    printf ("%s", text);
    for (i = 0; i < buffer_length; i++)
        printf (" %02X", buffer[i]);
    printf ("\n");
}

static gboolean
socket_data_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    int s;
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gssize n_read;

    s = g_io_channel_unix_get_fd (channel);
    n_read = recv (s, buffer, MAXIMUM_REQUEST_LENGTH, 0);
    if (n_read < 0)
        g_warning ("Error reading from socket: %s", strerror (errno));
    else if (n_read == 0)
    {
        g_debug ("EOF");
        return FALSE;
    }
    else
    {
        guint8 byte_order;
        guint16 protocol_major_version, protocol_minor_version;
        gchar *authorization_protocol_name;
        guint8 *authorization_protocol_data;
        guint16 authorization_protocol_data_length;
        gchar *auth_error = NULL;
        guint8 response_buffer[MAXIMUM_REQUEST_LENGTH];
        gsize n_written;

        log_buffer ("Read", buffer, n_read);

        decode_connect (buffer, n_read,
                        &byte_order,
                        &protocol_major_version, &protocol_minor_version,
                        &authorization_protocol_name,
                        &authorization_protocol_data, &authorization_protocol_data_length);
        g_debug ("Got connect request using protocol %d.%d and authorization '%s'", protocol_major_version, protocol_minor_version, authorization_protocol_name);

        notify_status ("XSERVER :%d ACCEPT-CONNECT", display_number);

        if (auth_path)
        {
            gchar *xauth_data;
            gsize xauth_length;
            GError *error = NULL;

            if (g_file_get_contents (auth_path, &xauth_data, &xauth_length, &error))
            {
                gsize offset;
                guint16 family, length, data_length;
                gchar *address, *number, *name;
                guint8 *data;

                family = read_card16 ((guint8 *) xauth_data, xauth_length, BYTE_ORDER_MSB, &offset);
                length = read_card16 ((guint8 *) xauth_data, xauth_length, BYTE_ORDER_MSB, &offset);
                address = (gchar *) read_string8 ((guint8 *) xauth_data, xauth_length, length, &offset);
                length = read_card16 ((guint8 *) xauth_data, xauth_length, BYTE_ORDER_MSB, &offset);
                number = (gchar *) read_string8 ((guint8 *) xauth_data, xauth_length, length, &offset);
                length = read_card16 ((guint8 *) xauth_data, xauth_length, BYTE_ORDER_MSB, &offset);
                name = (gchar *) read_string8 ((guint8 *) xauth_data, xauth_length, length, &offset);
                data_length = read_card16 ((guint8 *) xauth_data, xauth_length, BYTE_ORDER_MSB, &offset);
                data = read_string8 ((guint8 *) xauth_data, xauth_length, data_length, &offset);

                if (strcmp (authorization_protocol_name, "") == 0)
                    auth_error = g_strdup ("Authorization required");
                else if (strcmp (authorization_protocol_name, "MIT-MAGIC-COOKIE-1") == 0)
                {
                    gboolean matches = TRUE;
                    if (authorization_protocol_data_length == data_length)
                    {
                        guint16 i;
                        for (i = 0; i < data_length && authorization_protocol_data[i] == data[i]; i++);
                        matches = i == data_length;
                    }
                    else
                        matches = FALSE;
                    if (!matches)
                        auth_error = g_strdup_printf ("Invalid MIT-MAGIC-COOKIE key");
                }
                else
                    auth_error = g_strdup_printf ("Unknown authorization: '%s'", authorization_protocol_name);

                g_free (address);
                g_free (number);
                g_free (name);
                g_free (data);
            }
            else
                g_warning ("Error reading auth file: %s", error->message);
            g_clear_error (&error);
        }

        if (auth_error)
        {
            n_written = encode_failed (response_buffer, MAXIMUM_REQUEST_LENGTH, byte_order, auth_error);
            g_debug ("Sending Failed: %s", auth_error);
            g_free (auth_error);
        }
        else
        {
            n_written = encode_accept (response_buffer, MAXIMUM_REQUEST_LENGTH, byte_order);
            g_debug ("Sending Success");
        }

        send (s, response_buffer, n_written, 0);
        log_buffer ("Wrote", response_buffer, n_written);
    }

    return TRUE;
}

static gboolean
socket_connect_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    int s, data_socket;

    g_debug ("Got connection");

    s = g_io_channel_unix_get_fd (channel);
    data_socket = accept (s, NULL, NULL);
    if (data_socket < 0)
        g_warning ("Error accepting connection: %s", strerror (errno));
    else
        g_io_add_watch (g_io_channel_unix_new (data_socket), G_IO_IN, socket_data_cb, NULL);

    return TRUE;
}

static void
quit (int status)
{    
    if (lock_path)
        unlink (lock_path);
    if (socket_path)
        unlink (socket_path);

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

int
main (int argc, char **argv)
{
    int i, s;
    struct sockaddr_un address;
    char *pid_string;
    GMainLoop *loop;
    int lock_file;

    signal (SIGINT, signal_cb);
    signal (SIGTERM, signal_cb);
    signal (SIGHUP, signal_cb);

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
            {
                // FIXME: Disable TCP/IP socket
            }
        }
        else if (strcmp (arg, "-nr") == 0)
            ;
    }

    notify_status ("XSERVER :%d START", display_number);

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

    s = socket (AF_UNIX, SOCK_STREAM, 0);
    if (s < 0)
    {
        g_warning ("Error opening socket: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }

    socket_path = g_strdup_printf ("/tmp/.X11-unix/X%d", display_number);
    address.sun_family = AF_UNIX;
    strncpy (address.sun_path, socket_path, UNIX_PATH_MAX);
    if (bind (s, (struct sockaddr *) &address, sizeof (address)) < 0)
    {
        g_warning ("Error binding socket: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }

    if (listen (s, 10) < 0)
    {
        g_warning ("Error binding socket: %s", strerror (errno));
        quit (EXIT_FAILURE);
    }

    g_io_add_watch (g_io_channel_unix_new (s), G_IO_IN, socket_connect_cb, NULL);
  
    /* Indicate ready if parent process has requested it */
    indicate_ready ();

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
