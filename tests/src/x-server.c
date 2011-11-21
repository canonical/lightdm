#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include "x-common.h"
#include "x-server.h"

G_DEFINE_TYPE (XServer, x_server, G_TYPE_OBJECT);
G_DEFINE_TYPE (XClient, x_client, G_TYPE_OBJECT);

#define MAXIMUM_REQUEST_LENGTH 65535
#define VENDOR "LightDM"

enum
{
    Failed = 0,
    Success = 1,
    Authenticate = 2
};

enum
{
    Reply = 1,
};

enum {
    X_SERVER_CLIENT_CONNECTED,
    X_SERVER_CLIENT_DISCONNECTED,
    X_SERVER_LAST_SIGNAL
};
static guint x_server_signals[X_SERVER_LAST_SIGNAL] = { 0 };

struct XServerPrivate
{
    gint display_number;
    gboolean listen_unix;
    gboolean listen_tcp;
    gint tcp_port;
    gchar *socket_path;
    GSocket *unix_socket;
    GIOChannel *unix_channel;
    GSocket *tcp_socket;
    GIOChannel *tcp_channel;
    GHashTable *clients;

    GList *screens;
};

struct XClientPrivate
{
    GSocket *socket;  
    GIOChannel *channel;
    guint8 byte_order;
    gboolean connected;
    guint16 sequence_number;
};

enum
{
    X_CLIENT_CONNECT,
    X_CLIENT_CREATE_WINDOW,
    X_CLIENT_CHANGE_WINDOW_ATTRIBUTES,
    X_CLIENT_GET_WINDOW_ATTRIBUTES,
    X_CLIENT_DESTROY_WINDOW,
    X_CLIENT_DESTROY_SUBWINDOWS,
    X_CLIENT_CHANGE_SET_SAVE,
    X_CLIENT_REPARENT_WINDOW,
    X_CLIENT_MAP_WINDOW,
    X_CLIENT_MAP_SUBWINDOWS,
    X_CLIENT_UNMAP_WINDOW,
    X_CLIENT_UNMAP_SUBWINDOWS,
    X_CLIENT_CONFIGURE_WINDOW,
    X_CLIENT_CIRCULATE_WINDOW,
    X_CLIENT_GET_GEOMETRY,
    X_CLIENT_QUERY_TREE,
    X_CLIENT_INTERN_ATOM,
    X_CLIENT_GET_ATOM_NAME,
    X_CLIENT_CHANGE_PROPERTY,
    X_CLIENT_DELETE_PROPERTY,
    X_CLIENT_GET_PROPERTY,
    X_CLIENT_LIST_PROPERTIES,
    X_CLIENT_CREATE_PIXMAP,
    X_CLIENT_FREE_PIXMAP,
    X_CLIENT_CREATE_GC,
    X_CLIENT_CHANGE_GC,
    X_CLIENT_COPY_GC,
    X_CLIENT_FREE_GC,
    X_CLIENT_QUERY_EXTENSION,
    X_CLIENT_BELL,
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
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written = 0, length_offset;
    int i;

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, Success, &n_written);
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, X_PROTOCOL_MAJOR_VERSION, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, X_PROTOCOL_MINOR_VERSION, &n_written);
    length_offset = n_written;
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written);
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, X_RELEASE_NUMBER, &n_written);
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0x00a00000, &n_written); // resource-id-base
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0x001fffff, &n_written); // resource-id-mask
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); // motion-buffer-size
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, strlen (VENDOR), &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, MAXIMUM_REQUEST_LENGTH, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written); // number of screens
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 7, &n_written); // number of pixmap formats
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 0, &n_written); // image-byte-order
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 0, &n_written); // bitmap-format-bit-order
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // bitmap-format-scanline-unit
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // bitmap-format-scanline-pad
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 8, &n_written); // min-keycode
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 255, &n_written); // max-keycode
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);
    write_padded_string (buffer, MAXIMUM_REQUEST_LENGTH, VENDOR, &n_written);

    // LISTofFORMAT
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written); // depth
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written); // bits-per-pixel
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // scanline-pad
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 5, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written); // depth
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 8, &n_written); // bits-per-pixel
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // scanline-pad
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 5, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 8, &n_written); // depth
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 8, &n_written); // bits-per-pixel
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // scanline-pad
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 5, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 15, &n_written); // depth
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 16, &n_written); // bits-per-pixel
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // scanline-pad
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 5, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 16, &n_written); // depth
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 16, &n_written); // bits-per-pixel
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // scanline-pad
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 5, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 24, &n_written); // depth
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // bits-per-pixel
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // scanline-pad
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 5, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // depth
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // bits-per-pixel
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // scanline-pad
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 5, &n_written);

    // LISTofSCREEN
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 87, &n_written); // root
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 32, &n_written); // default-colormap
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0x00FFFFFF, &n_written); // white-pixel
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0x00000000, &n_written); // black-pixel
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, X_EVENT_StructureNotify | X_EVENT_SubstructureNotify | X_EVENT_SubstructureRedirect, &n_written); // SETofEVENT
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 1680, &n_written); // width-in-pixels
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 1050, &n_written); // height-in-pixels
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 569, &n_written); // width-in-millimeters
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 356, &n_written); // height-in-millimeters
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 1, &n_written); // min-installed-maps
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 1, &n_written); // max-installed-maps
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 34, &n_written); // root-visual
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 0, &n_written); // backing-stores
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 0, &n_written); // save-unders
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 24, &n_written); // root-depth
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 7, &n_written); // number of depths

    // LISTofDEPTH
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 24, &n_written); // depth
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 32, &n_written); // number of VISUALTYPES in visuals
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);
  
    // LISTofVISUALTYPE
    for (i = 0; i < 32; i++)
    {
        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 34 + i, &n_written); // visual-id
        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written); // class
        write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 8, &n_written); // bits-per-rgb-value
        write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 1, &n_written); // colormap-entries
        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0x00FF0000, &n_written); // red-mask
        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0x0000FF00, &n_written); // green-mask
        write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0x000000FF, &n_written); // blue-mask
        write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);
    }

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written); // depth
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); // number of VISUALTYPES in visuals
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written); // depth
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); // number of VISUALTYPES in visuals
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 8, &n_written); // depth
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); // number of VISUALTYPES in visuals
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 15, &n_written); // depth
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); // number of VISUALTYPES in visuals
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 16, &n_written); // depth
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); // number of VISUALTYPES in visuals
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, 32, &n_written); // depth
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written); // number of VISUALTYPES in visuals
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 4, &n_written);

    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, (n_written - length_offset) / 4, &length_offset);

    send (g_io_channel_unix_get_fd (client->priv->channel), buffer, n_written, 0);
}

void
x_client_send_query_extension_response (XClient *client, guint16 sequence_number, gboolean present, guint8 major_opcode, guint8 first_event, guint8 first_error)
{
    guint8 buffer[MAXIMUM_REQUEST_LENGTH];
    gsize n_written = 0;

    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, Reply, &n_written);
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 1, &n_written);
    write_card16 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, sequence_number, &n_written); // sequence-number
    write_card32 (buffer, MAXIMUM_REQUEST_LENGTH, client->priv->byte_order, 0, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, present ? 1 : 0, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, major_opcode, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, first_event, &n_written);
    write_card8 (buffer, MAXIMUM_REQUEST_LENGTH, first_error, &n_written);
    write_padding (buffer, MAXIMUM_REQUEST_LENGTH, 20, &n_written);

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
x_client_finalize (GObject *object)
{
}

static void
x_client_class_init (XClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = x_client_finalize;
    g_type_class_add_private (klass, sizeof (XClientPrivate));

    x_client_signals[X_CLIENT_CONNECT] =
        g_signal_new ("connect",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, connect),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_CHANGE_WINDOW_ATTRIBUTES] =
        g_signal_new ("change-window-attributes",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, change_window_attributes),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_GET_WINDOW_ATTRIBUTES] =
        g_signal_new ("get-window-attributes",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, get_window_attributes),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_CREATE_WINDOW] = 
        g_signal_new ("create-window",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, create_window),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_DESTROY_WINDOW] =
        g_signal_new ("destroy-window",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, destroy_window),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_DESTROY_SUBWINDOWS] =
        g_signal_new ("destroy-subwindows",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, destroy_subwindows),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_CHANGE_SET_SAVE] =
        g_signal_new ("change-set-save",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, change_set_save),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_REPARENT_WINDOW] =
        g_signal_new ("reparent-window",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, reparent_window),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_MAP_WINDOW] =
        g_signal_new ("map-window",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, map_window),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_MAP_SUBWINDOWS] =
        g_signal_new ("map-subwindows",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, map_subwindows),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_UNMAP_WINDOW] =
        g_signal_new ("unmap-window",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, unmap_window),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_UNMAP_SUBWINDOWS] =
        g_signal_new ("unmap-subwindows",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, unmap_subwindows),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_CONFIGURE_WINDOW] =
        g_signal_new ("configure-window",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, configure_window),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_CIRCULATE_WINDOW] =
        g_signal_new ("circulate-window",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, circulate_window),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_GET_GEOMETRY] =
        g_signal_new ("get-geometry",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, get_geometry),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_QUERY_TREE] =
        g_signal_new ("query-tree",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, query_tree),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_INTERN_ATOM] =
        g_signal_new ("intern-atom",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, intern_atom),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_GET_ATOM_NAME] =
        g_signal_new ("get-atom-name",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, get_atom_name),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_CHANGE_PROPERTY] =
        g_signal_new ("change-property",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, change_property),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_DELETE_PROPERTY] =
        g_signal_new ("delete-property",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, delete_property),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_GET_PROPERTY] =
        g_signal_new ("get_property",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, get_property),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_LIST_PROPERTIES] =
        g_signal_new ("list-properties",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, list_properties),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_CREATE_PIXMAP] =
        g_signal_new ("create_pixmap",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, create_pixmap),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_FREE_PIXMAP] =
        g_signal_new ("free_pixmap",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, free_pixmap),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_CREATE_GC] =
        g_signal_new ("create-gc",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, create_gc),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_CHANGE_GC] =
        g_signal_new ("change-gc",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, change_gc),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_COPY_GC] =
        g_signal_new ("copy-gc",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, copy_gc),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_FREE_GC] =
        g_signal_new ("free-gc",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, free_gc),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_QUERY_EXTENSION] =
        g_signal_new ("query-extension",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, query_extension),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__POINTER,
                      G_TYPE_NONE, 1, G_TYPE_POINTER);
    x_client_signals[X_CLIENT_BELL] =
        g_signal_new ("bell",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XClientClass, bell),
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
decode_create_window (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XCreateWindow *message;

    message = g_malloc0 (sizeof (XCreateWindow));
    message->depth = data;
    message->wid = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->parent = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->x = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    message->y = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    message->width = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    message->height = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    message->border_width = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    message->class = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    message->visual = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->value_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_background_pixmap) != 0)
        message->background_pixmap = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_background_pixel) != 0)
        message->background_pixel = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_border_pixmap) != 0)
        message->border_pixmap = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_border_pixel) != 0)
        message->border_pixel = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_bit_gravity) != 0)
        message->bit_gravity = read_card8 (buffer, buffer_length, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_win_gravity) != 0)
        message->win_gravity = read_card8 (buffer, buffer_length, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_backing_store) != 0)
        message->backing_store = read_card8 (buffer, buffer_length, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_backing_planes) != 0)
        message->backing_planes = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_backing_pixel) != 0)
        message->backing_pixel = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_override_redirect) != 0)
        message->override_redirect = read_card8 (buffer, buffer_length, offset) != 0;
    if ((message->value_mask & X_WINDOW_VALUE_MASK_save_under) != 0)
        message->save_under = read_card8 (buffer, buffer_length, offset) != 0;
    if ((message->value_mask & X_WINDOW_VALUE_MASK_event_mask) != 0)
        message->event_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_do_not_propagate_mask) != 0)
        message->do_not_propogate_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_colormap) != 0)
        message->colormap = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_cursor) != 0)
        message->cursor = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_CREATE_WINDOW], 0, message);

    g_free (message);
}

static void
decode_change_window_attributes (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XChangeWindowAttributes *message;

    message = g_malloc0 (sizeof (XChangeWindowAttributes));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->value_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_background_pixmap) != 0)
        message->background_pixmap = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_background_pixel) != 0)
        message->background_pixel = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_border_pixmap) != 0)
        message->border_pixmap = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_border_pixel) != 0)
        message->border_pixel = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_bit_gravity) != 0)
        message->bit_gravity = read_card8 (buffer, buffer_length, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_win_gravity) != 0)
        message->win_gravity = read_card8 (buffer, buffer_length, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_backing_store) != 0)
        message->backing_store = read_card8 (buffer, buffer_length, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_backing_planes) != 0)
        message->backing_planes = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_backing_pixel) != 0)
        message->backing_pixel = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_override_redirect) != 0)
        message->override_redirect = read_card8 (buffer, buffer_length, offset) != 0;
    if ((message->value_mask & X_WINDOW_VALUE_MASK_save_under) != 0)
        message->save_under = read_card8 (buffer, buffer_length, offset) != 0;
    if ((message->value_mask & X_WINDOW_VALUE_MASK_event_mask) != 0)
        message->event_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_do_not_propagate_mask) != 0)
        message->do_not_propogate_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_colormap) != 0)
        message->colormap = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_WINDOW_VALUE_MASK_cursor) != 0)
        message->cursor = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_CHANGE_WINDOW_ATTRIBUTES], 0, message);

    g_free (message);
}

static void
decode_get_window_attributes (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XGetWindowAttributes *message;

    message = g_malloc0 (sizeof (XGetWindowAttributes));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_GET_WINDOW_ATTRIBUTES], 0, message);

    g_free (message);
}

static void
decode_destroy_window (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XDestroyWindow *message;

    message = g_malloc0 (sizeof (XDestroyWindow));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
  
    g_signal_emit (client, x_client_signals [X_CLIENT_DESTROY_WINDOW], 0, message);

    g_free (message);
}

static void
decode_destroy_subwindows (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XDestroySubwindows *message;

    message = g_malloc0 (sizeof (XDestroySubwindows));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
  
    g_signal_emit (client, x_client_signals [X_CLIENT_DESTROY_SUBWINDOWS], 0, message);

    g_free (message);
}

static void
decode_change_set_save (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XChangeSetSave *message;

    message = g_malloc (sizeof (XChangeSetSave));
    message->mode = data;
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_CHANGE_SET_SAVE], 0, message);

    g_free (message);
}

static void
decode_reparent_window (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XReparentWindow *message;

    message = g_malloc (sizeof (XReparentWindow));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->parent = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->x = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    message->y = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_REPARENT_WINDOW], 0, message);

    g_free (message);
}

static void
decode_map_window (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XMapWindow *message;

    message = g_malloc0 (sizeof (XMapWindow));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_MAP_WINDOW], 0, message);

    g_free (message);
}

static void
decode_map_subwindows (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XMapSubwindows *message;

    message = g_malloc0 (sizeof (XMapSubwindows));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_MAP_SUBWINDOWS], 0, message);

    g_free (message);
}

static void
decode_unmap_window (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XUnmapWindow *message;

    message = g_malloc0 (sizeof (XUnmapWindow));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_UNMAP_WINDOW], 0, message);
  
    g_free (message);
}

static void
decode_unmap_subwindows (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XUnmapSubwindows *message;

    message = g_malloc0 (sizeof (XUnmapSubwindows));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_UNMAP_SUBWINDOWS], 0, message);
  
    g_free (message);
}

static void
decode_configure_window (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XConfigureWindow *message;

    message = g_malloc0 (sizeof (XConfigureWindow));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->value_mask = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_CONFIGURE_WINDOW_VALUE_MASK_x) != 0)
        message->x = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_CONFIGURE_WINDOW_VALUE_MASK_y) != 0)
        message->y = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_CONFIGURE_WINDOW_VALUE_MASK_width) != 0)
        message->width = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_CONFIGURE_WINDOW_VALUE_MASK_height) != 0)
        message->height = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_CONFIGURE_WINDOW_VALUE_MASK_border_width) != 0)
        message->border_width = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_CONFIGURE_WINDOW_VALUE_MASK_sibling) != 0)
        message->sibling = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_CONFIGURE_WINDOW_VALUE_MASK_stack_mode) != 0)
        message->stack_mode = read_card8 (buffer, buffer_length, offset);

    g_signal_emit (client, x_client_signals [X_CLIENT_CONFIGURE_WINDOW], 0, message);
  
    g_free (message);
}

static void
decode_circulate_window (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XCirculateWindow *message;

    message = g_malloc0 (sizeof (XCirculateWindow));
    message->direction = data;
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
  
    g_signal_emit (client, x_client_signals[X_CLIENT_CIRCULATE_WINDOW], 0, message);
  
    g_free (message);
}

static void
decode_get_geometry (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XGetGeometry *message;

    message = g_malloc0 (sizeof (XGetGeometry));
    message->drawable = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_GET_GEOMETRY], 0, message);
  
    g_free (message);
}

static void
decode_query_tree (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XQueryTree *message;

    message = g_malloc0 (sizeof (XQueryTree));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
  
    g_signal_emit (client, x_client_signals[X_CLIENT_QUERY_TREE], 0, message);
  
    g_free (message);
}

static void
decode_intern_atom (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XInternAtom *message;
    guint16 name_length;
  
    message = g_malloc0 (sizeof (XInternAtom));

    message->only_if_exists = data != 0;
    name_length = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    read_padding (2, offset);
    message->name = read_padded_string (buffer, buffer_length, name_length, offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_INTERN_ATOM], 0, message);

    g_free (message->name);
    g_free (message);
}

static void
decode_get_atom_name (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XGetAtomName *message;

    message = g_malloc0 (sizeof (XGetAtomName));
    message->atom = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_GET_ATOM_NAME], 0, message);

    g_free (message);
}

static void
decode_change_property (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XChangeProperty *message;

    message = g_malloc0 (sizeof (XChangeProperty));
    message->mode = data;
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->property = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->type = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->format = read_card8 (buffer, buffer_length, offset);
    read_padding (3, offset);    
    message->length = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->data = read_string8 (buffer, buffer_length, message->length * message->format / 8, offset);
    read_padding (pad (message->length * message->format / 8), offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_CHANGE_PROPERTY], 0, message);

    g_free (message);
}

static void
decode_delete_property (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XDeleteProperty *message;

    message = g_malloc0 (sizeof (XDeleteProperty));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->property = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
  
    g_signal_emit (client, x_client_signals[X_CLIENT_DELETE_PROPERTY], 0, message);
  
    g_free (message);
}

static void
decode_get_property (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XGetProperty *message;

    message = g_malloc0 (sizeof (XGetProperty));

    message->delete = data != 0;
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->property = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->type = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->long_offset = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->long_length = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_GET_PROPERTY], 0, message);
  
    g_free (message);
}

static void
decode_list_properties (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XListProperties *message;

    message = g_malloc0 (sizeof (XListProperties));
    message->window = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_LIST_PROPERTIES], 0, message);
  
    g_free (message);
}

static void
decode_create_pixmap (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XCreatePixmap *message;

    message = g_malloc0 (sizeof (XCreatePixmap));
    message->depth = data;
    message->pid = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->drawable = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->width = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    message->height = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_CREATE_PIXMAP], 0, message);

    g_free (message);
}

static void
decode_free_pixmap (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XFreePixmap *message;

    message = g_malloc0 (sizeof (XFreePixmap));
    message->pixmap = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_FREE_PIXMAP], 0, message);

    g_free (message);
}

static void
decode_create_gc (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XCreateGC *message;
  
    message = g_malloc0 (sizeof (XCreateGC));

    message->cid = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->drawable = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->value_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_function) != 0)
    {      
        message->function = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_plane_mask) != 0)
        message->plane_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_foreground) != 0)
        message->foreground = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_background) != 0)
        message->background = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_line_width) != 0)
    {
        message->line_width = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_line_style) != 0)
    {
        message->line_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_cap_style) != 0)
    {
        message->cap_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_join_style) != 0)
    {
        message->join_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_fill_style) != 0)
    {
        message->fill_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_fill_rule) != 0)
    {
        message->fill_rule = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_tile) != 0)
        message->tile = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_stipple) != 0)
        message->stipple = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_tile_stipple_x_origin) != 0)
    {
        message->tile_stipple_x_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_tile_stipple_y_origin) != 0)
    {
        message->tile_stipple_y_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_font) != 0)
        message->font = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_subwindow_mode) != 0)
    {
        message->subwindow_mode = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_graphics_exposures) != 0)
    {
        message->graphics_exposures = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_clip_x_origin) != 0)
    {
        message->clip_x_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_clip_y_origin) != 0)
    {
        message->clip_y_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_clip_mask) != 0)
        message->clip_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_dash_offset) != 0)
    {
        message->dash_offset = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_dashes) != 0)
    {
        message->dashes = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_arc_mode) != 0)
    {
        message->arc_mode = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }

    g_signal_emit (client, x_client_signals[X_CLIENT_CREATE_GC], 0, message);
  
    g_free (message);
}

static void
decode_change_gc (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XChangeGC *message;

    message = g_malloc0 (sizeof (XChangeGC));
    message->gc = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->value_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_function) != 0)
    {      
        message->function = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_plane_mask) != 0)
        message->plane_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_foreground) != 0)
        message->foreground = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_background) != 0)
        message->background = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_line_width) != 0)
    {
        message->line_width = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_line_style) != 0)
    {
        message->line_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_cap_style) != 0)
    {
        message->cap_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_join_style) != 0)
    {
        message->join_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_fill_style) != 0)
    {
        message->fill_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_fill_rule) != 0)
    {
        message->fill_rule = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_tile) != 0)
        message->tile = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_stipple) != 0)
        message->stipple = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_tile_stipple_x_origin) != 0)
    {
        message->tile_stipple_x_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_tile_stipple_y_origin) != 0)
    {
        message->tile_stipple_y_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_font) != 0)
        message->font = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_subwindow_mode) != 0)
    {
        message->subwindow_mode = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_graphics_exposures) != 0)
    {
        message->graphics_exposures = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_clip_x_origin) != 0)
    {
        message->clip_x_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_clip_y_origin) != 0)
    {
        message->clip_y_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_clip_mask) != 0)
        message->clip_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_dash_offset) != 0)
    {
        message->dash_offset = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_dashes) != 0)
    {
        message->dashes = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_arc_mode) != 0)
    {
        message->arc_mode = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }

    g_signal_emit (client, x_client_signals[X_CLIENT_CHANGE_GC], 0, message);

    g_free (message);
}

static void
decode_copy_gc (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XCopyGC *message;

    message = g_malloc0 (sizeof (XCopyGC));
    message->src_gc = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->dst_gc = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    message->value_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_function) != 0)
    {      
        message->function = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_plane_mask) != 0)
        message->plane_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_foreground) != 0)
        message->foreground = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_background) != 0)
        message->background = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_line_width) != 0)
    {
        message->line_width = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_line_style) != 0)
    {
        message->line_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_cap_style) != 0)
    {
        message->cap_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_join_style) != 0)
    {
        message->join_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_fill_style) != 0)
    {
        message->fill_style = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_fill_rule) != 0)
    {
        message->fill_rule = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_tile) != 0)
        message->tile = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_stipple) != 0)
        message->stipple = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_tile_stipple_x_origin) != 0)
    {
        message->tile_stipple_x_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_tile_stipple_y_origin) != 0)
    {
        message->tile_stipple_y_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_font) != 0)
        message->font = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_subwindow_mode) != 0)
    {
        message->subwindow_mode = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_graphics_exposures) != 0)
    {
        message->graphics_exposures = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_clip_x_origin) != 0)
    {
        message->clip_x_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_clip_y_origin) != 0)
    {
        message->clip_y_origin = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_clip_mask) != 0)
        message->clip_mask = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);
    if ((message->value_mask & X_GC_VALUE_MASK_dash_offset) != 0)
    {
        message->dash_offset = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
        read_padding (2, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_dashes) != 0)
    {
        message->dashes = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }
    if ((message->value_mask & X_GC_VALUE_MASK_arc_mode) != 0)
    {
        message->arc_mode = read_card8 (buffer, buffer_length, offset);
        read_padding (3, offset);
    }

    g_signal_emit (client, x_client_signals[X_CLIENT_COPY_GC], 0, message);

    g_free (message);
}

static void
decode_free_gc (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XFreeGC *message;

    message = g_malloc0 (sizeof (XFreeGC));
    message->gc = read_card32 (buffer, buffer_length, client->priv->byte_order, offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_FREE_GC], 0, message);

    g_free (message);
}

static void
decode_query_extension (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XQueryExtension *message;
    guint16 name_length;

    message = g_malloc0 (sizeof (XQueryExtension));

    name_length = read_card16 (buffer, buffer_length, client->priv->byte_order, offset);
    read_padding (2, offset);
    message->name = read_padded_string (buffer, buffer_length, name_length, offset);

    g_signal_emit (client, x_client_signals[X_CLIENT_QUERY_EXTENSION], 0, message);

    g_free (message->name);
    g_free (message);
}

static void
decode_bell (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
    XBell *message;

    message = g_malloc0 (sizeof (XBell));
    message->percent = data;

    g_signal_emit (client, x_client_signals[X_CLIENT_BELL], 0, message);

    g_free (message);
}

static void
decode_no_operation (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
}
                     
static void
decode_big_req_enable (XClient *client, guint16 sequence_number, guint8 data, const guint8 *buffer, gssize buffer_length, gsize *offset)
{
}

static void
decode_request (XClient *client, guint16 sequence_number, const guint8 *buffer, gssize buffer_length)
{
    int opcode;
    gsize offset = 0;

    while (offset < buffer_length)
    {
        guint8 data;
        guint16 length, remaining;
        gsize start_offset;

        start_offset = offset;
        opcode = read_card8 (buffer, buffer_length, &offset);
        data = read_card8 (buffer, buffer_length, &offset);
        length = read_card16 (buffer, buffer_length, client->priv->byte_order, &offset) * 4;
        remaining = start_offset + length;
      
        g_debug ("Got opcode=%d length=%d", opcode, length);

        switch (opcode)
        {
        case 1:
            decode_create_window (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 2:
            decode_change_window_attributes (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 3:
            decode_get_window_attributes (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 4:
            decode_destroy_window (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 5:
            decode_destroy_subwindows (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 6:
            decode_change_set_save (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 7:
            decode_reparent_window (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 8:
            decode_map_window (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 9:
            decode_map_subwindows (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 10:
            decode_unmap_window (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 11:
            decode_unmap_subwindows (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 12:
            decode_configure_window (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 13:
            decode_circulate_window (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 14:
            decode_get_geometry (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 15:
            decode_query_tree (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 16:
            decode_intern_atom (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 17:
            decode_get_atom_name (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 18:
            decode_change_property (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 19:
            decode_delete_property (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 20:
            decode_get_property (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 21:
            decode_list_properties (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 53:
            decode_create_pixmap (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 54:
            decode_free_pixmap (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 55:
            decode_create_gc (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 56:
            decode_change_gc (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 57:
            decode_copy_gc (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 60:
            decode_free_gc (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 98:
            decode_query_extension (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 104:
            decode_bell (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 127:
            decode_no_operation (client, sequence_number, data, buffer, remaining, &offset);
            break;
        case 135:
            decode_big_req_enable (client, sequence_number, data, buffer, remaining, &offset);
            break;
        default:
            g_debug ("Ignoring unknown opcode %d", opcode);
            break;
        }

        offset = start_offset + length;
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
        {
            decode_request (client, client->priv->sequence_number, buffer, n_read);
            client->priv->sequence_number++;
        }
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
    server->priv->clients = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) g_io_channel_unref, g_object_unref);
    server->priv->listen_unix = TRUE;
    server->priv->listen_tcp = TRUE;
}

static void
x_server_finalize (GObject *object)
{
    XServer *server = (XServer *) object;
    if (server->priv->socket_path)
        unlink (server->priv->socket_path);
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
