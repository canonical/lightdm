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
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include "status.h"

static GKeyFile *config;

static gchar *socket_path = NULL;
static gchar *lock_path = NULL;
static gchar *auth_path = NULL;

typedef struct
{
    GIOChannel *channel;
    guint8 byte_order;
} Connection;
static GHashTable *connections;

static int display_number = 0;

static gboolean listen_unix = TRUE;
static gboolean listen_tcp = TRUE;
static GSocket *unix_socket = NULL;
static GIOChannel *unix_channel = NULL;
static GSocket *tcp_socket = NULL;
static GIOChannel *tcp_channel = NULL;

static int xdmcp_port = 177;
static gboolean do_xdmcp = FALSE;
static gchar *xdmcp_host = NULL;
static GSocket *xdmcp_socket = NULL;
static guint xdmcp_query_timer = 0;

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

typedef enum
{
    XDMCP_BroadcastQuery = 1,
    XDMCP_Query          = 2,
    XDMCP_IndirectQuery  = 3,
    XDMCP_ForwardQuery   = 4,
    XDMCP_Willing        = 5,
    XDMCP_Unwilling      = 6,
    XDMCP_Request        = 7,
    XDMCP_Accept         = 8,
    XDMCP_Decline        = 9,
    XDMCP_Manage         = 10,
    XDMCP_Refuse         = 11,
    XDMCP_Failed         = 12,
    XDMCP_KeepAlive      = 13,
    XDMCP_Alive          = 14
} XDMCPOpcode;

static void
cleanup ()
{
    if (lock_path)
        unlink (lock_path);
    if (socket_path)
        unlink (socket_path);
}

static void
quit (int status)
{
    cleanup ();
    exit (status);
}

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
read_card8 (const guint8 *buffer, gsize buffer_length, gsize *offset)
{
    if (*offset >= buffer_length)
        return 0;
    (*offset)++;
    return buffer[*offset - 1];
}

static guint16
read_card16 (const guint8 *buffer, gsize buffer_length, guint8 byte_order, gsize *offset)
{
    guint8 a, b;

    a = read_card8 (buffer, buffer_length, offset);
    b = read_card8 (buffer, buffer_length, offset);
    if (byte_order == BYTE_ORDER_MSB)
        return a << 8 | b;
    else
        return b << 8 | a;
}

static guint32
read_card32 (const guint8 *buffer, gsize buffer_length, guint8 byte_order, gsize *offset)
{
    guint8 a, b, c, d;

    a = read_card8 (buffer, buffer_length, offset);
    b = read_card8 (buffer, buffer_length, offset);
    c = read_card8 (buffer, buffer_length, offset);
    d = read_card8 (buffer, buffer_length, offset);
    if (byte_order == BYTE_ORDER_MSB)
        return a << 24 | b << 16 | c << 8 | d;
    else
        return d << 24 | c << 16 | b << 8 | a;
}

static guint8 *
read_string8 (const guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset)
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
read_string (const guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset)
{
    return (gchar *) read_string8 (buffer, buffer_length, string_length, offset);
}

static gchar *
read_padded_string (const guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset)
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
    return (strlen (value) + pad (strlen (value))) / 4;
}

static void
write_string (guint8 *buffer, gsize buffer_length, const gchar *value, gsize *offset)
{
    write_string8 (buffer, buffer_length, (guint8 *) value, strlen (value), offset);
}

static void
write_padded_string (guint8 *buffer, gsize buffer_length, const gchar *value, gsize *offset)
{
    write_string8 (buffer, buffer_length, (guint8 *) value, strlen (value), offset);
    write_padding (buffer, buffer_length, pad (strlen (value)), offset);
}

static void
decode_connect (const guint8 *buffer, gsize buffer_length,
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

static gchar *
make_hex_string (const guint8 *buffer, gsize buffer_length)
{
    GString *text;
    gsize i;
    gchar *result;

    text = g_string_new ("");
    for (i = 0; i < buffer_length; i++)
    {
        if (i > 0)
            g_string_append (text, " ");
        g_string_append_printf (text, "%02X", buffer[i]);
    }

    result = text->str;
    g_string_free (text, FALSE);
    return result;
}

static void
log_buffer (const gchar *text, const guint8 *buffer, gsize buffer_length)
{
    gchar *hex;

    hex = make_hex_string (buffer, buffer_length);
    printf ("%s %s\n", text, hex);
    g_free (hex);
}

static void
decode_connection_request (GIOChannel *channel, const guint8 *buffer, gssize buffer_length)
{
    guint8 byte_order;
    guint16 protocol_major_version, protocol_minor_version;
    gchar *authorization_protocol_name;
    guint8 *authorization_protocol_data;
    guint16 authorization_protocol_data_length;
    gchar *hex;
    gchar *auth_error = NULL;
    guint8 response_buffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written;

    decode_connect (buffer, buffer_length,
                    &byte_order,
                    &protocol_major_version, &protocol_minor_version,
                    &authorization_protocol_name,
                    &authorization_protocol_data, &authorization_protocol_data_length);
    hex = make_hex_string (authorization_protocol_data, authorization_protocol_data_length);
    g_debug ("Got connect request using protocol %d.%d and authorization '%s' with data '%s'", protocol_major_version, protocol_minor_version, authorization_protocol_name, hex);
    g_free (hex);

    notify_status ("XSERVER :%d ACCEPT-CONNECT", display_number);

    if (auth_path)
    {
        gchar *xauth_data;
        gsize xauth_length;
        GError *error = NULL;

        if (g_file_get_contents (auth_path, &xauth_data, &xauth_length, &error))
        {
            gsize offset = 0;
            guint16 length, data_length;
            gchar *address, *number, *name;
            guint8 *data;

            /*family =*/ read_card16 ((guint8 *) xauth_data, xauth_length, BYTE_ORDER_MSB, &offset);
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
                {
                    gchar *hex1, *hex2;
                    hex1 = make_hex_string (authorization_protocol_data, authorization_protocol_data_length);
                    hex2 = make_hex_string (data, data_length);
                    g_debug ("MIT-MAGIC-COOKIE mismatch, got '%s', expect '%s'", hex1, hex2);
                    g_free (hex1);
                    g_free (hex2);
                    auth_error = g_strdup_printf ("Invalid MIT-MAGIC-COOKIE key");
                }
            }
            else
                auth_error = g_strdup_printf ("Unknown authorization: '%s'", authorization_protocol_name);

            g_free (address);
            g_free (number);
            g_free (name);
            g_free (data);
        }
        else
        {
            g_warning ("Error reading auth file: %s", error->message);
            auth_error = g_strdup ("No authorization database");
        }
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
        Connection *connection;

        g_debug ("Sending Success");
        n_written = encode_accept (response_buffer, MAXIMUM_REQUEST_LENGTH, byte_order);

        /* Store connection */
        connection = g_malloc0 (sizeof (Connection));
        connection->channel = g_io_channel_ref (channel);
        connection->byte_order = byte_order;
        g_hash_table_insert (connections, channel, connection);
    }

    send (g_io_channel_unix_get_fd (channel), response_buffer, n_written, 0);
    log_buffer ("Wrote X", response_buffer, n_written);
}

static void
decode_intern_atom (Connection *connection, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    gboolean only_if_exists;
    guint16 name_length;
    gchar *name;

    only_if_exists = read_card8 (buffer, buffer_length, offset) != 0;
    read_padding (2, offset);
    name_length = read_card16 (buffer, buffer_length, connection->byte_order, offset);
    read_padding (2, offset);
    name = read_padded_string (buffer, buffer_length, name_length, offset);
  
    g_debug ("InternAtom only-if-exits=%s name=%s", only_if_exists ? "True" : "False", name);

    if (strcmp (name, "SIGSEGV") == 0)
    {
        notify_status ("XSERVER :%d CRASH", display_number);
        cleanup ();
        kill (getpid (), SIGSEGV);
    }
}

static void
decode_request (Connection *connection, const guint8 *buffer, gssize buffer_length)
{
    int opcode;
    gsize offset = 0;

    opcode = read_card8 (buffer, buffer_length, &offset);
    switch (opcode)
    {
    case 16:
        decode_intern_atom (connection, buffer, buffer_length, &offset);
        break;
    default:
        g_debug ("Ignoring unknown opcode %d", opcode);
        break;
    }
}

static gboolean
socket_data_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gssize n_read;

    n_read = recv (g_io_channel_unix_get_fd (channel), buffer, MAXIMUM_REQUEST_LENGTH, 0);
    if (n_read < 0)
        g_warning ("Error reading from socket: %s", strerror (errno));
    else if (n_read == 0)
    {
        g_debug ("EOF");
        return FALSE;
    }
    else
    {
        Connection *connection;

        log_buffer ("Read X", buffer, n_read);

        connection = g_hash_table_lookup (connections, channel);
        if (connection)
            decode_request (connection, buffer, n_read);
        else
            decode_connection_request (channel, buffer, n_read);
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
xdmcp_write (const guint8 *buffer, gssize buffer_length)
{
    gssize n_written;
    GError *error = NULL;

    n_written = g_socket_send (xdmcp_socket, (const gchar *) buffer, buffer_length, NULL, &error);
    if (n_written < 0)
        g_warning ("Failed to send XDMCP request: %s", error->message);
    else if (n_written != buffer_length)
        g_warning ("Partial write for XDMCP request, wrote %zi, expected %zi", n_written, buffer_length);
    g_clear_error (&error);

    log_buffer ("Wrote XDMCP", buffer, buffer_length);
}

static void
decode_willing (const guint8 *buffer, gssize buffer_length)
{
    gsize offset = 0;
    guint16 length;
    gchar *authentication_name, *hostname, *status;
    GSocketAddress *local_address;
    const guint8 *native_address;
    gssize native_address_length;
    guint8 response[MAXIMUM_REQUEST_LENGTH];

    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    authentication_name = read_string (buffer, buffer_length, length, &offset);
    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    hostname = read_string (buffer, buffer_length, length, &offset);
    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    status = read_string (buffer, buffer_length, length, &offset);

    if (xdmcp_query_timer == 0)
    {
        g_debug ("Ignoring XDMCP unrequested/duplicate Willing");
        return;
    }

    notify_status ("XSERVER :%d GOT-WILLING AUTHENTICATION-NAME=\"%s\" HOSTNAME=\"%s\" STATUS=\"%s\"", display_number, authentication_name, hostname, status);

    /* Stop sending queries */
    g_source_remove (xdmcp_query_timer);
    xdmcp_query_timer = 0;

    local_address = g_socket_get_local_address (xdmcp_socket, NULL);
    GInetAddress *inet_address = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (local_address));
    native_address_length = g_inet_address_get_native_size (inet_address);
    native_address = g_inet_address_to_bytes (inet_address);

    offset = 0;
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, 1, &offset); /* version = 1 */
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, XDMCP_Request, &offset);
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, 17 + native_address_length + strlen ("") + strlen ("MIT-MAGIC-COOKIE-1") + strlen ("TEST XSERVER"), &offset);

    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, display_number, &offset);
    write_card8 (response, MAXIMUM_REQUEST_LENGTH, 1, &offset); /* 1 address */
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, 0, &offset); /* FamilyInternet */
    write_card8 (response, MAXIMUM_REQUEST_LENGTH, 1, &offset); /* 1 address */
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, native_address_length, &offset);
    write_string8 (response, MAXIMUM_REQUEST_LENGTH, native_address, native_address_length, &offset);
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, strlen (""), &offset);
    write_string (response, MAXIMUM_REQUEST_LENGTH, "", &offset);
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, 0, &offset); /* No authentication data */
    write_card8 (response, MAXIMUM_REQUEST_LENGTH, 1, &offset); /* 1 authorization */
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, strlen ("MIT-MAGIC-COOKIE-1"), &offset);
    write_string (response, MAXIMUM_REQUEST_LENGTH, "MIT-MAGIC-COOKIE-1", &offset);
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, strlen ("TEST XSERVER"), &offset);
    write_string (response, MAXIMUM_REQUEST_LENGTH, "TEST XSERVER", &offset);

    notify_status ("XSERVER :%d SEND-REQUEST DISPLAY-NUMBER=%d AUTHORIZATION-NAME=\"%s\" MFID=\"%s\"", display_number, display_number, "MIT-MAGIC-COOKIE-1", "TEST XSERVER");

    xdmcp_write (response, offset); 
}

static void
decode_accept (const guint8 *buffer, gssize buffer_length)
{
    gsize offset = 0;
    guint16 length;
    guint32 session_id;
    gchar *authentication_name, *authorization_name;
    guint8 response[MAXIMUM_REQUEST_LENGTH];

    session_id = read_card32 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    authentication_name = read_string (buffer, buffer_length, length, &offset);
    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    read_string8 (buffer, buffer_length, length, &offset);
    authorization_name = read_string (buffer, buffer_length, length, &offset);
    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    read_string8 (buffer, buffer_length, length, &offset);

    notify_status ("XSERVER :%d GOT-ACCEPT SESSION-ID=%d AUTHENTICATION-NAME=\"%s\" AUTHORIZATION-NAME=\"%s\"", display_number, session_id, authentication_name, authorization_name);

    offset = 0;
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, 1, &offset); /* version = 1 */
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, XDMCP_Manage, &offset);
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, 8 + strlen ("DISPLAY CLASS"), &offset);

    write_card32 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, session_id, &offset);
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, display_number, &offset);
    write_card16 (response, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, strlen ("DISPLAY CLASS"), &offset);
    write_string (response, MAXIMUM_REQUEST_LENGTH, "DISPLAY CLASS", &offset);

    notify_status ("XSERVER :%d SEND-MANAGE SESSION-ID=%d DISPLAY-NUMBER=%d DISPLAY-CLASS=\"%s\"", display_number, session_id, display_number, "DISPLAY CLASS");

    xdmcp_write (response, offset);
}

static void
decode_decline (const guint8 *buffer, gssize buffer_length)
{
    gsize offset = 0;
    guint16 length;
    gchar *authentication_name, *status;

    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    status = read_string (buffer, buffer_length, length, &offset);
    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    authentication_name = read_string (buffer, buffer_length, length, &offset);
    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    read_string8 (buffer, buffer_length, length, &offset);

    notify_status ("XSERVER :%d GOT-DECLINE STATUS=\"%s\" AUTHENTICATION-NAME=\"%s\"", display_number, status, authentication_name);
}

static void
decode_failed (const guint8 *buffer, gssize buffer_length)
{
    gsize offset = 0;
    guint16 length;
    guint32 session_id;
    gchar *status;

    session_id = read_card32 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    length = read_card16 (buffer, buffer_length, BYTE_ORDER_MSB, &offset);
    status = read_string (buffer, buffer_length, length, &offset);

    notify_status ("XSERVER :%d GOT-FAILED SESSION-ID=%d STATUS=\"%s\"", display_number, session_id, status);
}

static gboolean
xdmcp_data_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gssize n_read;

    n_read = recv (g_io_channel_unix_get_fd (channel), buffer, MAXIMUM_REQUEST_LENGTH, 0);
    if (n_read < 0)
        g_warning ("Error reading from XDMCP socket: %s", strerror (errno));
    else if (n_read == 0)
    {
        g_debug ("EOF");
        return FALSE;
    }
    else
    {
        gsize offset = 0;
        guint16 version, opcode, length;

        log_buffer ("Read XDMCP", buffer, n_read);

        version = read_card16 (buffer, n_read, BYTE_ORDER_MSB, &offset);
        opcode = read_card16 (buffer, n_read, BYTE_ORDER_MSB, &offset);
        length = read_card16 (buffer, n_read, BYTE_ORDER_MSB, &offset);

        if (version != 1)
        {
            g_debug ("Ignoring XDMCP version %d message", version);
            return TRUE;
        }
        if (6 + length > n_read)
        {
            g_debug ("Ignoring XDMCP message of length %zi with invalid length field %d", n_read, length);
            return TRUE;
        }
        switch (opcode)
        {
        case XDMCP_Willing:
            decode_willing (buffer + offset, n_read - offset);
            break;

        case XDMCP_Accept:
            decode_accept (buffer + offset, n_read - offset);
            break;

        case XDMCP_Decline:
            decode_decline (buffer + offset, n_read - offset);
            break;

        case XDMCP_Failed:
            decode_failed (buffer + offset, n_read - offset);
            break;

        default:
            g_debug ("Ignoring unknown XDMCP opcode %d", opcode);
            break;
        }
    }

    return TRUE;
}

static gboolean
xdmcp_query_cb (gpointer data)
{
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gsize offset = 0;
    static gboolean notified_query = FALSE;

    g_debug ("Send XDMCP Query");
    if (!notified_query)
    {
        notify_status ("XSERVER :%d SEND-QUERY", display_number);
        notified_query = TRUE;
    }

    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, 1, &offset); /* version = 1 */
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, XDMCP_Query, &offset);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, BYTE_ORDER_MSB, 1, &offset);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 0, &offset);

    xdmcp_write (buffer, offset);

    return TRUE;
}

int
main (int argc, char **argv)
{
    int i;
    char *pid_string;
    GMainLoop *loop;
    int lock_file;
    GError *error = NULL;

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
            g_debug ("Loading authorization from %s", auth_path);
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
            /* Ignore VT arg */
        }
        else
        {
            g_printerr ("Unrecognized option: %s\n"
                        "Use: %s [:<display>] [option]\n"
                        "-auth file             Select authorization file\n"
                        "-nolisten string       Don't listen on protocol\n"
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

    notify_status ("XSERVER :%d START", display_number);

    config = g_key_file_new ();
    if (g_getenv ("TEST_CONFIG"))
        g_key_file_load_from_file (config, g_getenv ("TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

    if (g_key_file_has_key (config, "test-xserver-config", "return-value", NULL))
    {
        int return_value = g_key_file_get_integer (config, "test-xserver-config", "return-value", NULL);
        notify_status ("XSERVER :%d EXIT CODE=%d", display_number, return_value);
        return return_value;
    }

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

    connections = g_hash_table_new (g_direct_hash, g_direct_equal);

    if (listen_unix)
    {
        socket_path = g_strdup_printf ("/tmp/.X11-unix/X%d", display_number);
        unix_socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
        if (!unix_socket ||
            !g_socket_bind (unix_socket, g_unix_socket_address_new (socket_path), TRUE, &error) ||
            !g_socket_listen (unix_socket, &error))
        {
            g_warning ("Error creating Unix X socket: %s", error->message);
            quit (EXIT_FAILURE);
        }
        unix_channel = g_io_channel_unix_new (g_socket_get_fd (unix_socket));
        g_io_add_watch (unix_channel, G_IO_IN, socket_connect_cb, NULL);
    }

    if (listen_tcp)
    {
        tcp_socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
        if (!tcp_socket ||
            !g_socket_bind (tcp_socket, g_inet_socket_address_new (g_inet_address_new_any (G_SOCKET_FAMILY_IPV4), 6000 + display_number), TRUE, &error) ||
            !g_socket_listen (tcp_socket, &error))
        {
            g_warning ("Error creating TCP/IP X socket: %s", error->message);
            quit (EXIT_FAILURE);
        }
        tcp_channel = g_io_channel_unix_new (g_socket_get_fd (tcp_socket));
        g_io_add_watch (tcp_channel, G_IO_IN, socket_connect_cb, NULL);
    }

    /* Enable XDMCP */
    if (do_xdmcp)
    {
        GSocketConnectable *address;
        GSocketAddress *socket_address;

        xdmcp_socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &error);
      
        address = g_network_address_new (xdmcp_host, xdmcp_port);
        socket_address = g_socket_address_enumerator_next (g_socket_connectable_enumerate (address), NULL, NULL);

        if (!xdmcp_socket ||
            !g_socket_connect (xdmcp_socket, socket_address, NULL, NULL) ||
            !g_io_add_watch (g_io_channel_unix_new (g_socket_get_fd (xdmcp_socket)), G_IO_IN, xdmcp_data_cb, &error))
        {
            g_warning ("Error creating XDMCP socket: %s", error->message);
            quit (EXIT_FAILURE);
        }
    }

    /* Indicate ready if parent process has requested it */
    indicate_ready ();

    /* Send first query */
    if (do_xdmcp)
    {
        xdmcp_query_timer = g_timeout_add (2000, xdmcp_query_cb, NULL);
        xdmcp_query_cb (NULL);
    }

    g_main_loop_run (loop);

    return EXIT_SUCCESS;
}
