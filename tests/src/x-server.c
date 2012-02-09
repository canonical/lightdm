/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*- */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include "x-common.h"
#include "x-server.h"

G_DEFINE_TYPE (XServer, x_server, G_TYPE_OBJECT);
G_DEFINE_TYPE (XScreen, x_screen, G_TYPE_OBJECT);
G_DEFINE_TYPE (XVisual, x_visual, G_TYPE_OBJECT);
G_DEFINE_TYPE (XClient, x_client, G_TYPE_OBJECT);

#define MAXIMUM_REQUEST_LENGTH 65535

enum
{
    Failed = 0,
    Success = 1,
    Authenticate = 2
};

enum
{
    Error = 0,
    Reply = 1,
};

enum
{
    InternAtom = 16,
    GetProperty = 20,
    QueryExtension = 98,
    kbUseExtension = 200
};

enum
{
    BadAtom = 5,
    BadImplementation = 17
};

enum {
    X_SERVER_CLIENT_CONNECTED,
    X_SERVER_CLIENT_DISCONNECTED,
    X_SERVER_LAST_SIGNAL
};
static guint x_server_signals[X_SERVER_LAST_SIGNAL] = { 0 };

typedef struct
{
    guint8 depth;
    guint8 bits_per_pixel;
    guint8 scanline_pad;
} PixmapFormat;

struct XServerPrivate
{
    gchar *vendor;

    gint display_number;
  
    guint32 motion_buffer_size;
    guint8 image_byte_order;
    guint8 bitmap_format_bit_order;
  
    guint8 min_keycode;
    guint8 max_keycode;

    GList *pixmap_formats;  
    GList *screens;

    gboolean listen_unix;
    gboolean listen_tcp;
    gint tcp_port;
    gchar *socket_path;
    GSocket *unix_socket;
    GIOChannel *unix_channel;
    GSocket *tcp_socket;
    GIOChannel *tcp_channel;
    GHashTable *clients;
    GHashTable *atoms;
    gint next_atom_index;
};

struct XClientPrivate
{
    XServer *server;
    GSocket *socket;  
    GIOChannel *channel;
    guint8 byte_order;
    gboolean connected;
    guint16 sequence_number;
};

struct XScreenPrivate
{
    guint32 white_pixel;
    guint32 black_pixel;
    guint32 current_input_masks;
    guint16 width_in_pixels;
    guint16 height_in_pixels;
    guint16 width_in_millimeters;
    guint16 height_in_millimeters;
    GList *visuals;
};

struct XVisualPrivate
{
    guint32 id;
    guint8 depth;
    guint8 class;
    guint8 bits_per_rgb_value;
    guint16 colormap_entries;
    guint32 red_mask;
    guint32 green_mask;
    guint32 blue_mask;
};

enum
{
    X_CLIENT_CONNECT,
    X_CLIENT_DISCONNECTED,
    X_CLIENT_LAST_SIGNAL
};
static guint x_client_signals[X_CLIENT_LAST_SIGNAL] = { 0 };

GInetAddress *
x_client_get_address (XClient *client)
{
    GSocketAddress *socket_address;
    GError *error = NULL;

    socket_address = g_socket_get_remote_address (client->priv->socket, &error);
    if (error)
        g_warning ("Error getting remote socket address");
    g_clear_error (&error);
    if (!socket_address)
        return NULL;

    if (G_IS_INET_SOCKET_ADDRESS (socket_address))
        return g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (socket_address));
    else
        return NULL;
}

void
x_client_send_failed (XClient *client, const gchar *reason)
{
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written = 0, length_offset;

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, Failed, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, strlen (reason), &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, X_PROTOCOL_MAJOR_VERSION, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, X_PROTOCOL_MINOR_VERSION, &n_written);
    length_offset = n_written;
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written);
    write_padded_string (buffer, MAXIMUM_REQUEST_LENGTH, reason, &n_written);

    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, (n_written - length_offset) / 4, &length_offset);

    send (g_io_channel_unix_get_fd (client->priv->channel), buffer, n_written, 0);
}

void 
x_client_send_success (XClient *client)
{
    XServer *server = client->priv->server;
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written = 0, length_offset;
    GList *link;

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, Success, &n_written);
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, X_PROTOCOL_MAJOR_VERSION, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, X_PROTOCOL_MINOR_VERSION, &n_written);
    length_offset = n_written;
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written);
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, X_RELEASE_NUMBER, &n_written);
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0x00a00000, &n_written); // resource-id-base
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0x001fffff, &n_written); // resource-id-mask
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, server->priv->motion_buffer_size, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, strlen (client->priv->server->priv->vendor), &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, MAXIMUM_REQUEST_LENGTH, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, g_list_length (server->priv->screens), &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, g_list_length (server->priv->pixmap_formats), &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, server->priv->image_byte_order, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, server->priv->bitmap_format_bit_order, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // bitmap-format-scanline-unit
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // bitmap-format-scanline-pad
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, server->priv->min_keycode, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, server->priv->max_keycode, &n_written);
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);
    write_padded_string (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->server->priv->vendor, &n_written);

    for (link = server->priv->pixmap_formats; link; link = link->next)
    {
        PixmapFormat *format = link->data;
        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, format->depth, &n_written);
        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, format->bits_per_pixel, &n_written);
        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, format->scanline_pad, &n_written);
        write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 5, &n_written);
    }

    for (link = server->priv->screens; link; link = link->next)
    {
        XScreen *screen = link->data;
        guint8 depth, n_depths = 0;
        gsize n_depths_offset;

        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 87, &n_written); // root
        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 32, &n_written); // default-colormap
        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, screen->priv->white_pixel, &n_written);
        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, screen->priv->black_pixel, &n_written);
        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, screen->priv->current_input_masks, &n_written);
        write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, screen->priv->width_in_pixels, &n_written);
        write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, screen->priv->height_in_pixels, &n_written);
        write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, screen->priv->width_in_millimeters, &n_written);
        write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, screen->priv->height_in_millimeters, &n_written);
        write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 1, &n_written); // min-installed-maps
        write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 1, &n_written); // max-installed-maps
        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 34, &n_written); // root-visual
        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 0, &n_written); // backing-stores
        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 0, &n_written); // save-unders
        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 24, &n_written); // root-depth
        n_depths_offset = n_written;
        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 0, &n_written);

        depth = 0;
        while (TRUE)
        {
            GList *visual_link;
            guint16 n_visuals = 0;

            /* Find the next depth to this one */
            guint8 next_depth = 255;
            for (visual_link = screen->priv->visuals; visual_link; visual_link = visual_link->next)
            {
                XVisual *visual = visual_link->data;
                if (visual->priv->depth > depth && visual->priv->depth < next_depth)
                    next_depth = visual->priv->depth;
            }
            if (next_depth == 255)
                break;
            depth = next_depth;
            n_depths++;

            for (visual_link = screen->priv->visuals; visual_link; visual_link = visual_link->next)
            {
                XVisual *visual = visual_link->data;
                if (visual->priv->depth == depth)
                    n_visuals++;
            }

            write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, depth, &n_written);
            write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
            write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, n_visuals, &n_written);
            write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);

            for (visual_link = screen->priv->visuals; visual_link; visual_link = visual_link->next)
            {
                XVisual *visual = visual_link->data;

                if (visual->priv->depth != depth)
                    continue;

                write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, visual->priv->id, &n_written);
                write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, visual->priv->class, &n_written);
                write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, visual->priv->bits_per_rgb_value, &n_written);
                write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, visual->priv->colormap_entries, &n_written);
                write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, visual->priv->red_mask, &n_written);
                write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, visual->priv->green_mask, &n_written);
                write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, visual->priv->blue_mask, &n_written);
                write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);
            }
        }

        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, n_depths, &n_depths_offset);
    }

    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, (n_written - length_offset) / 4, &length_offset);

    send (g_io_channel_unix_get_fd (client->priv->channel), buffer, n_written, 0);
}

void
x_client_send_error (XClient *client, int type, int major, int minor)
{
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written = 0;

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, Error, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, type, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, client->priv->sequence_number, &n_written);
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); /* resourceID */
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, minor, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, major, &n_written);
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 21, &n_written);

    send (g_io_channel_unix_get_fd (client->priv->channel), buffer, n_written, 0);
}

void
x_client_disconnect (XClient *client)
{
    g_io_channel_shutdown (client->priv->channel, TRUE, NULL);
}

static void
x_client_init (XClient *client)
{
    client->priv = G_TYPE_INSTANCE_GET_PRIVATE (client, x_client_get_type (), XClientPrivate);
    client->priv->sequence_number = 1;
}

static void
x_client_class_init (XClientClass *klass)
{
    g_type_class_add_private (klass, sizeof (XClientPrivate));

    x_client_signals[X_CLIENT_CONNECT] =
        g_signal_new ("connect",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, connect),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_DISCONNECTED] =
        g_signal_new ("disconnected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, disconnected),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}

XServer *
x_server_new (gint display_number)
{
    XServer *server = g_object_new (x_server_get_type (), NULL);
    server->priv->display_number = display_number;
    server->priv->tcp_port = 6000 + display_number;
    return server;
}

XScreen *
x_server_add_screen (XServer *server, guint32 white_pixel, guint32 black_pixel, guint32 current_input_masks, guint16 width_in_pixels, guint16 height_in_pixels, guint16 width_in_millimeters, guint16 height_in_millimeters)
{
    XScreen *screen;

    screen = g_object_new (x_screen_get_type (), NULL);

    screen->priv->white_pixel = white_pixel;
    screen->priv->black_pixel = black_pixel;
    screen->priv->current_input_masks = current_input_masks;
    screen->priv->width_in_pixels = width_in_pixels;
    screen->priv->height_in_pixels = height_in_pixels;
    screen->priv->width_in_millimeters = width_in_millimeters;
    screen->priv->height_in_millimeters = height_in_millimeters;
  
    server->priv->screens = g_list_append (server->priv->screens, screen);

    return screen;
}

void
x_server_add_pixmap_format (XServer *server, guint8 depth, guint8 bits_per_pixel, guint8 scanline_pad)
{
    PixmapFormat *format;
  
    format = g_malloc0 (sizeof (PixmapFormat));
    format->depth = depth;
    format->bits_per_pixel = bits_per_pixel;
    format->scanline_pad = scanline_pad;
    server->priv->pixmap_formats = g_list_append (server->priv->pixmap_formats, format);
}

void
x_server_set_listen_unix (XServer *server, gboolean listen_unix)
{
    server->priv->listen_unix = listen_unix;
}

void
x_server_set_listen_tcp (XServer *server, gboolean listen_tcp)
{
    server->priv->listen_tcp = listen_tcp;
}

XVisual *
x_screen_add_visual (XScreen *screen, guint8 depth, guint8 class, guint8 bits_per_rgb_value, guint16 colormap_entries, guint32 red_mask, guint32 green_mask, guint32 blue_mask)
{
    XVisual *visual;

    visual = g_object_new (x_visual_get_type (), NULL);
    visual->priv->id = 0; // FIXME
    visual->priv->depth = depth;
    visual->priv->class = class;
    visual->priv->bits_per_rgb_value = bits_per_rgb_value;
    visual->priv->colormap_entries = colormap_entries;
    visual->priv->red_mask = red_mask;
    visual->priv->green_mask = green_mask;
    visual->priv->blue_mask = blue_mask;
    
    return visual;
}

static void
decode_connection_request (XClient *client, const guint8 *buffer, gssize buffer_length)
{
    guint8 byte_order;
    gsize offset = 0;
    guint16 n;
    XConnect *message;

    byte_order = read_card8 (buffer, buffer_length, &offset);
    if (!(byte_order == 'B' || byte_order == 'l'))
    {
        g_warning ("Invalid byte order");
        return;
    }
  
    message = g_malloc0 (sizeof (XConnect));

    message->byte_order = byte_order == 'B' ? X_BYTE_ORDER_MSB : X_BYTE_ORDER_LSB;
    read_padding (1, &offset);
    message->protocol_major_version = read_card16 (buffer, buffer_length, message->byte_order, &offset);
    message->protocol_minor_version = read_card16 (buffer, buffer_length, message->byte_order, &offset);
    n = read_card16 (buffer, buffer_length, message->byte_order, &offset);
    message->authorization_protocol_data_length = read_card16 (buffer, buffer_length, message->byte_order, &offset);
    read_padding (2, &offset);
    message->authorization_protocol_name = read_padded_string (buffer, buffer_length, n, &offset);
    message->authorization_protocol_data = read_string8 (buffer, buffer_length, message->authorization_protocol_data_length, &offset);
    read_padding (pad (message->authorization_protocol_data_length), &offset);

    /* Store information about the client */
    client->priv->byte_order = message->byte_order;
    client->priv->connected = TRUE;

    g_signal_emit (client, x_client_signals[X_CLIENT_CONNECT], 0, message);

    g_free (message->authorization_protocol_name);
    g_free (message->authorization_protocol_data);
    g_free (message);
}

static void
process_intern_atom (XClient *client, const guint8 *buffer, gssize buffer_length)
{
    /* Decode */

    gsize offset = 0;
    guint8 onlyIfExists;
    guint16 n;
    gchar *name;
    int atom;

    read_padding (1, &offset); /* reqType */
    onlyIfExists = read_card8 (buffer, buffer_length, &offset);
    read_padding (2, &offset); /* length */
    n = read_card16 (buffer, buffer_length, client->priv->byte_order, &offset);
    read_padding (2, &offset);
    name = read_padded_string (buffer, buffer_length, n, &offset);

    /* Process */

    atom = client->priv->server->priv->next_atom_index++;

    if (onlyIfExists)
    {
        g_free (name);
        if (!g_hash_table_contains (client->priv->server->priv->atoms, GINT_TO_POINTER (atom)))
        {
            x_client_send_error (client, BadAtom, InternAtom, 0);
            return;
        }
    }
    else
        g_hash_table_insert (client->priv->server->priv->atoms, GINT_TO_POINTER (atom), name);

    /* Reply */

    guint8 outBuffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written = 0;

    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, Reply, &n_written);
    write_padding (outBuffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, client->priv->sequence_number, &n_written);
    write_card32 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); /* length */
    write_card32 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, atom, &n_written);
    write_padding (outBuffer, MAXIMUM_REQUEST_LENGTH, 20, &n_written);

    send (g_io_channel_unix_get_fd (client->priv->channel), outBuffer, n_written, 0);
}

static void
process_get_property (XClient *client, const guint8 *buffer, gssize buffer_length)
{
    /* Decode */

    gsize offset = 0;
    guint8 delete;
    guint32 property;
    guint32 type;

    read_padding (1, &offset); /* reqType */
    delete = read_card8 (buffer, buffer_length, &offset);
    read_padding (2, &offset); /* length */
    read_padding (4, &offset); /* window */
    property = read_card32 (buffer, buffer_length, client->priv->byte_order, &offset);
    type = read_card32 (buffer, buffer_length, client->priv->byte_order, &offset);
    read_padding (4, &offset); /* longOffset */
    read_padding (4, &offset); /* longLength */

    /* Process */

    gchar *name = g_hash_table_lookup (client->priv->server->priv->atoms, GINT_TO_POINTER (property));
    GString *reply = NULL;
    guint8 format = 8;

    if (g_strcmp0 (name, "_XKB_RULES_NAMES") == 0)
    {
        GKeyFile *config;

        config = g_key_file_new ();
        if (g_getenv ("LIGHTDM_TEST_CONFIG"))
            g_key_file_load_from_file (config, g_getenv ("LIGHTDM_TEST_CONFIG"), G_KEY_FILE_NONE, NULL);

        reply = g_string_new ("");

        g_string_append (reply, "evdev"); /* rules file */
        g_string_append_c (reply, 0); /* embedded null byte */

        g_string_append (reply, "pc105"); /* model name */
        g_string_append_c (reply, 0); /* embedded null byte */

        if (g_key_file_has_key (config, "test-xserver-config", "keyboard-layout", NULL))
            g_string_append (reply, g_key_file_get_string (config, "test-xserver-config", "keyboard-layout", NULL));
        else
            g_string_append (reply, "us");
        g_string_append_c (reply, 0); /* embedded null byte */

        if (g_key_file_has_key (config, "test-xserver-config", "keyboard-variant", NULL))
            g_string_append (reply, g_key_file_get_string (config, "test-xserver-config", "keyboard-variant", NULL));
        g_string_append_c (reply, 0); /* embedded null byte */

        /* no xkb options */
        g_string_append_c (reply, 0); /* embedded null byte */

        g_key_file_free (config);
    }

    if (name && delete)
        g_hash_table_remove (client->priv->server->priv->atoms, GINT_TO_POINTER (property));

    /* Reply */

    if (!reply)
    {
        x_client_send_error (client, BadImplementation, GetProperty, 0);
        return;
    }

    guint8 outBuffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written = 0, length_offset, packet_start;

    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, Reply, &n_written);
    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, format, &n_written);
    write_card16 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, client->priv->sequence_number, &n_written);
    length_offset = n_written;
    write_card32 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); /* length */
    write_card32 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, type, &n_written);
    write_card32 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); /* bytesAfter */
    write_card32 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, reply->len, &n_written);
    write_padding (outBuffer, MAXIMUM_REQUEST_LENGTH, 12, &n_written);
    packet_start = n_written;

    write_string8 (outBuffer, MAXIMUM_REQUEST_LENGTH, (guint8 *) reply->str, reply->len, &n_written);
    write_padding (outBuffer, MAXIMUM_REQUEST_LENGTH, pad (reply->len), &n_written);

    write_card32 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, (n_written - packet_start) / 4, &length_offset);

    send (g_io_channel_unix_get_fd (client->priv->channel), outBuffer, n_written, 0);

    /* Cleanup */

    g_string_free (reply, TRUE);
}

static void
process_query_extension (XClient *client, const guint8 *buffer, gssize buffer_length)
{
    /* Decode */

    gsize offset = 0;
    guint8 n;
    gchar *name;

    read_padding (1, &offset); /* reqType */
    read_padding (1, &offset); /* pad */
    read_padding (2, &offset); /* length */
    n = read_card16 (buffer, buffer_length, client->priv->byte_order, &offset);
    read_padding (2, &offset); /* pad */
    name = read_padded_string (buffer, buffer_length, n, &offset);

    /* Process */

    guint8 present = 0;
    if (g_strcmp0 (name, "XKEYBOARD") == 0)
        present = 1;

    /* Reply */

    guint8 outBuffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written = 0;

    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, Reply, &n_written);
    write_padding (outBuffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, client->priv->sequence_number, &n_written);
    write_card32 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); /* length */
    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, present, &n_written);
    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, kbUseExtension, &n_written); /* major_opcode */
    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, 0, &n_written); /* first_event */
    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, 0, &n_written); /* first_error */
    write_padding (outBuffer, MAXIMUM_REQUEST_LENGTH, 20, &n_written);

    send (g_io_channel_unix_get_fd (client->priv->channel), outBuffer, n_written, 0);

    /* Cleanup */

    g_free (name);
}

static void
process_kb_use_extension (XClient *client, const guint8 *buffer, gssize buffer_length)
{
    /* Nothing to decode, we don't care about parameters */

    /* Reply */

    guint8 outBuffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written = 0;

    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, Reply, &n_written);
    write_card8 (outBuffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written); /* supported */
    write_card16 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, client->priv->sequence_number, &n_written);
    write_card32 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); /* length */
    write_card16 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 1, &n_written); /* serverMajor */
    write_card16 (outBuffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); /* serverMinor */
    write_padding (outBuffer, MAXIMUM_REQUEST_LENGTH, 20, &n_written);

    send (g_io_channel_unix_get_fd (client->priv->channel), outBuffer, n_written, 0);
}

static void
decode_request (XClient *client, const guint8 *buffer, gssize buffer_length)
{
    int opcode;
    gsize offset = 0;

    while (offset < buffer_length)
    {
        gsize start_offset;
        guint16 length;

        start_offset = offset;
        opcode = read_card8 (buffer, buffer_length, &offset);
        read_card8 (buffer, buffer_length, &offset);
        length = read_card16 (buffer, buffer_length, client->priv->byte_order, &offset) * 4;

        g_debug ("Got opcode=%d length=%d", opcode, length);
        offset = start_offset + length;

        switch (opcode)
        {
        case InternAtom:
            process_intern_atom (client, buffer + start_offset, length);
            break;
        case GetProperty:
            process_get_property (client, buffer + start_offset, length);
            break;
        case QueryExtension:
            process_query_extension (client, buffer + start_offset, length);
            break;
        case kbUseExtension:
            process_kb_use_extension (client, buffer + start_offset, length);
            break;
        default:
            /* Send an error because we don't understand the opcode yet */
            x_client_send_error (client, BadImplementation, opcode, 0);
            break;
        }

        client->priv->sequence_number++;
    }
}

static gboolean
socket_data_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    XClient *client = data;
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gssize n_read;

    n_read = recv (g_io_channel_unix_get_fd (channel), buffer, MAXIMUM_REQUEST_LENGTH, 0);
    if (n_read < 0)
        g_warning ("Error reading from socket: %s", strerror (errno));
    else if (n_read == 0)
    {
        g_signal_emit (client, x_client_signals[X_CLIENT_DISCONNECTED], 0);
        return FALSE;
    }
    else
    {
        if (client->priv->connected)
            decode_request (client, buffer, n_read);
        else
            decode_connection_request (client, buffer, n_read);
    }

    return TRUE;
}

static void
x_client_disconnected_cb (XClient *client, XServer *server)
{
    g_signal_handlers_disconnect_matched (client, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, server);
    g_hash_table_remove (server->priv->clients, client->priv->channel);
    g_signal_emit (server, x_server_signals[X_SERVER_CLIENT_DISCONNECTED], 0, client);
}

static gboolean
socket_connect_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    XServer *server = data;
    GSocket *data_socket;
    XClient *client;
    GError *error = NULL;

    if (channel == server->priv->unix_channel)
        data_socket = g_socket_accept (server->priv->unix_socket, NULL, &error);
    else
        data_socket = g_socket_accept (server->priv->tcp_socket, NULL, &error);
    if (error)
        g_warning ("Error accepting connection: %s", strerror (errno));
    g_clear_error (&error);
    if (!data_socket)
        return FALSE;

    client = g_object_new (x_client_get_type (), NULL);
    client->priv->server = server;
    g_signal_connect (client, "disconnected", G_CALLBACK (x_client_disconnected_cb), server);
    client->priv->socket = data_socket;
    client->priv->channel = g_io_channel_unix_new (g_socket_get_fd (data_socket));
    g_hash_table_insert (server->priv->clients, client->priv->channel, client);
    g_io_add_watch (client->priv->channel, G_IO_IN, socket_data_cb, client);

    g_signal_emit (server, x_server_signals[X_SERVER_CLIENT_CONNECTED], 0, client);

    return TRUE;
}

gboolean
x_server_start (XServer *server)
{
    if (server->priv->listen_unix)
    {
        GError *error = NULL;
      
        server->priv->socket_path = g_strdup_printf ("/tmp/.X11-unix/X%d", server->priv->display_number);

        server->priv->unix_socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
        if (!server->priv->unix_socket ||
            !g_socket_bind (server->priv->unix_socket, g_unix_socket_address_new (server->priv->socket_path), TRUE, &error) ||
            !g_socket_listen (server->priv->unix_socket, &error))
        {
            g_warning ("Error creating Unix X socket: %s", error->message);
            return FALSE;
        }
        server->priv->unix_channel = g_io_channel_unix_new (g_socket_get_fd (server->priv->unix_socket));
        g_io_add_watch (server->priv->unix_channel, G_IO_IN, socket_connect_cb, server);
    }

    if (server->priv->listen_tcp)
    {
        GError *error = NULL;

        server->priv->tcp_socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
        if (!server->priv->tcp_socket ||
            !g_socket_bind (server->priv->tcp_socket, g_inet_socket_address_new (g_inet_address_new_any (G_SOCKET_FAMILY_IPV4), server->priv->tcp_port), TRUE, &error) ||
            !g_socket_listen (server->priv->tcp_socket, &error))
        {
            g_warning ("Error creating TCP/IP X socket: %s", error->message);
            return FALSE;
        }
        server->priv->tcp_channel = g_io_channel_unix_new (g_socket_get_fd (server->priv->tcp_socket));
        g_io_add_watch (server->priv->tcp_channel, G_IO_IN, socket_connect_cb, server);
    }

    return TRUE;
}

gsize
x_server_get_n_clients (XServer *server)
{
    return g_hash_table_size (server->priv->clients);
}

static void
x_server_init (XServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, x_server_get_type (), XServerPrivate);
    server->priv->vendor = g_strdup ("");
    server->priv->min_keycode = 8;
    server->priv->min_keycode = 255;
    server->priv->screens = NULL;
    server->priv->listen_unix = TRUE;
    server->priv->listen_tcp = TRUE;
    server->priv->clients = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) g_io_channel_unref, g_object_unref);
    server->priv->atoms = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
    server->priv->next_atom_index = 1;
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("PRIMARY"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("SECONDARY"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("ARC"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("ATOM"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("BITMAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CARDINAL"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("COLORMAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CURSOR"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CUT_BUFFER0"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CUT_BUFFER1"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CUT_BUFFER2"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CUT_BUFFER3"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CUT_BUFFER4"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CUT_BUFFER5"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CUT_BUFFER6"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CUT_BUFFER7"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("DRAWABLE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("FONT"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("INTEGER"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("PIXMAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("POINT"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RECTANGLE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RESOURCE_MANAGER"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RGB_COLOR_MAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RGB_BEST_MAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RGB_BLUE_MAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RGB_DEFAULT_MAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RGB_GRAY_MAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RGB_GREEN_MAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RGB_RED_MAP"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("STRING"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("VISUALID"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WINDOW"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_COMMAND"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_HINTS"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_CLIENT_MACHINE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_ICON_NAME"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_ICON_SIZE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_NAME"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_NORMAL_HINTS"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_SIZE_HINTS"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_ZOOM_HINTS"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("MIN_SPACE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("NORM_SPACE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("MAX_SPACE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("END_SPACE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("SUPERSCRIPT_X"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("SUPERSCRIPT_Y"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("SUBSCRIPT_X"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("SUBSCRIPT_Y"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("UNDERLINE_POSITION"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("UNDERLINE_THICKNESS"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("STRIKEOUT_ASCENT"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("STRIKEOUT_DESCENT"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("ITALIC_ANGLE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("X_HEIGHT"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("QUAD_WIDTH"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WEIGHT"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("POINT_SIZE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("RESOLUTION"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("COPYRIGHT"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("NOTICE"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("FONT_NAME"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("FAMILY_NAME"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("FULL_NAME"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("CAP_HEIGHT"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_CLASS"));
    g_hash_table_insert (server->priv->atoms, GINT_TO_POINTER (server->priv->next_atom_index++), g_strdup ("WM_TRANSIENT_FOR"));
}

static void
x_server_finalize (GObject *object)
{
    XServer *server = (XServer *) object;
    g_free (server->priv->vendor);
    if (server->priv->socket_path)
        unlink (server->priv->socket_path);
    g_hash_table_unref (server->priv->atoms);
    G_OBJECT_CLASS (x_server_parent_class)->finalize (object);
}

static void
x_server_class_init (XServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = x_server_finalize;
    g_type_class_add_private (klass, sizeof (XServerPrivate));
    x_server_signals[X_SERVER_CLIENT_CONNECTED] =
        g_signal_new ("client-connected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XServerClass, client_connected),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, x_client_get_type ());
    x_server_signals[X_SERVER_CLIENT_DISCONNECTED] =
        g_signal_new ("client-disconnected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XServerClass, client_disconnected),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, x_client_get_type ());
}

static void
x_screen_init (XScreen *screen)
{
    screen->priv = G_TYPE_INSTANCE_GET_PRIVATE (screen, x_screen_get_type (), XScreenPrivate);
}

static void
x_screen_class_init (XScreenClass *klass)
{
    g_type_class_add_private (klass, sizeof (XScreenPrivate));
}

static void
x_visual_init (XVisual *visual)
{
    visual->priv = G_TYPE_INSTANCE_GET_PRIVATE (visual, x_visual_get_type (), XVisualPrivate);
}

static void
x_visual_class_init (XVisualClass *klass)
{
    g_type_class_add_private (klass, sizeof (XVisualPrivate));
}
