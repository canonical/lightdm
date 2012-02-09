#ifndef _X_SERVER_H_
#define _X_SERVER_H_

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define X_PROTOCOL_MAJOR_VERSION 11
#define X_PROTOCOL_MINOR_VERSION 0

#define X_RELEASE_NUMBER 0

typedef struct
{
    guint8 byte_order;
    guint16 protocol_major_version, protocol_minor_version;
    gchar *authorization_protocol_name;
    guint8 *authorization_protocol_data;
    guint16 authorization_protocol_data_length;
} XConnect;

typedef struct XClientPrivate XClientPrivate;

typedef struct
{
   GObject         parent_instance;
   XClientPrivate *priv;
} XClient;

typedef struct
{
   GObjectClass parent_class;
   void (*connect)(XClient *client, XConnect *message);
   void (*disconnected)(XClient *client);
} XClientClass;

typedef struct XScreenPrivate XScreenPrivate;

typedef struct
{
   GObject         parent_instance;
   XScreenPrivate *priv;
} XScreen;

typedef struct
{
   GObjectClass parent_class;
} XScreenClass;

typedef struct XVisualPrivate XVisualPrivate;

typedef struct
{
   GObject         parent_instance;
   XVisualPrivate *priv;
} XVisual;

typedef struct
{
   GObjectClass parent_class;
} XVisualClass;

typedef struct XServerPrivate XServerPrivate;

typedef struct
{
   GObject         parent_instance;
   XServerPrivate *priv;
} XServer;

typedef struct
{
   GObjectClass parent_class;
   void (*client_connected)(XServer *server, XClient *client);
   void (*client_disconnected)(XServer *server, XClient *client);
} XServerClass;

GType x_server_get_type (void);

XServer *x_server_new (gint display_number);

XScreen *x_server_add_screen (XServer *server, guint32 white_pixel, guint32 black_pixel, guint32 current_input_masks, guint16 width_in_pixels, guint16 height_in_pixels, guint16 width_in_millimeters, guint16 height_in_millimeters);

void x_server_add_pixmap_format (XServer *server, guint8 depth, guint8 bits_per_pixel, guint8 scanline_pad);

void x_server_set_listen_unix (XServer *server, gboolean listen_unix);

void x_server_set_listen_tcp (XServer *server, gboolean listen_tcp);

gboolean x_server_start (XServer *server);

gsize x_server_get_n_clients (XServer *server);

GType x_screen_get_type (void);

XVisual *x_screen_add_visual (XScreen *screen, guint8 depth, guint8 class, guint8 bits_per_rgb_value, guint16 colormap_entries, guint32 red_mask, guint32 green_mask, guint32 blue_mask);

GType x_visual_get_type (void);

GType x_client_get_type (void);

GInetAddress *x_client_get_address (XClient *client);

void x_client_send_failed (XClient *client, const gchar *reason);

void x_client_send_success (XClient *client);

void x_client_send_error (XClient *client, int type, int major, int minor);

void x_client_disconnect (XClient *client);

G_END_DECLS

#endif /* _X_SERVER_H_ */
