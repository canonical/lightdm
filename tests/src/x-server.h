#ifndef _X_SERVER_H_
#define _X_SERVER_H_

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define X_PROTOCOL_MAJOR_VERSION 11
#define X_PROTOCOL_MINOR_VERSION 0

#define X_RELEASE_NUMBER 0

typedef enum
{
    X_EVENT_KeyPress             = 0x00000001,
    X_EVENT_KeyRelease           = 0x00000002,
    X_EVENT_ButtonPress          = 0x00000004,
    X_EVENT_ButtonRelease        = 0x00000008,
    X_EVENT_EnterWindow          = 0x00000010,
    X_EVENT_LeaveWindow          = 0x00000020,
    X_EVENT_PointerMotion        = 0x00000040,
    X_EVENT_PointerMotionHint    = 0x00000080,
    X_EVENT_Button1Motion        = 0x00000100,
    X_EVENT_Button2Motion        = 0x00000200,
    X_EVENT_Button3Motion        = 0x00000400,
    X_EVENT_Button4Motion        = 0x00000800,
    X_EVENT_Button5Motion        = 0x00001000,
    X_EVENT_ButtonMotion         = 0x00002000,
    X_EVENT_KeymapState          = 0x00004000,
    X_EVENT_Exposure             = 0x00008000,
    X_EVENT_VisibilityChange     = 0x00010000,
    X_EVENT_StructureNotify      = 0x00020000,
    X_EVENT_ResizeRedirect       = 0x00040000,
    X_EVENT_SubstructureNotify   = 0x00080000,
    X_EVENT_SubstructureRedirect = 0x00100000,
    X_EVENT_FocusChange          = 0x00200000,
    X_EVENT_PropertyChange       = 0x00400000,
    X_EVENT_ColormapChange       = 0x00800000,
    X_EVENT_OwnerGrabButton      = 0x01000000
} XEvent;

enum
{
    X_GC_VALUE_MASK_function              = 0x00000001,
    X_GC_VALUE_MASK_plane_mask            = 0x00000002,
    X_GC_VALUE_MASK_foreground            = 0x00000004,
    X_GC_VALUE_MASK_background            = 0x00000008,
    X_GC_VALUE_MASK_line_width            = 0x00000010,
    X_GC_VALUE_MASK_line_style            = 0x00000020,
    X_GC_VALUE_MASK_cap_style             = 0x00000040,
    X_GC_VALUE_MASK_join_style            = 0x00000080,
    X_GC_VALUE_MASK_fill_style            = 0x00000100,
    X_GC_VALUE_MASK_fill_rule             = 0x00000200,
    X_GC_VALUE_MASK_tile                  = 0x00000400,
    X_GC_VALUE_MASK_stipple               = 0x00000800,
    X_GC_VALUE_MASK_tile_stipple_x_origin = 0x00001000,
    X_GC_VALUE_MASK_tile_stipple_y_origin = 0x00002000,
    X_GC_VALUE_MASK_font                  = 0x00004000,
    X_GC_VALUE_MASK_subwindow_mode        = 0x00008000,
    X_GC_VALUE_MASK_graphics_exposures    = 0x00010000,
    X_GC_VALUE_MASK_clip_x_origin         = 0x00020000,
    X_GC_VALUE_MASK_clip_y_origin         = 0x00040000,
    X_GC_VALUE_MASK_clip_mask             = 0x00080000,
    X_GC_VALUE_MASK_dash_offset           = 0x00100000,
    X_GC_VALUE_MASK_dashes                = 0x00200000,
    X_GC_VALUE_MASK_arc_mode              = 0x00400000
} XGCValueMask;

typedef enum
{
    X_CREATE_WINDOW_VALUE_MASK_background_pixmap     = 0x00000001,
    X_CREATE_WINDOW_VALUE_MASK_background_pixel      = 0x00000002,
    X_CREATE_WINDOW_VALUE_MASK_border_pixmap         = 0x00000004,
    X_CREATE_WINDOW_VALUE_MASK_border_pixel          = 0x00000008,
    X_CREATE_WINDOW_VALUE_MASK_bit_gravity           = 0x00000010,
    X_CREATE_WINDOW_VALUE_MASK_win_gravity           = 0x00000020,
    X_CREATE_WINDOW_VALUE_MASK_backing_store         = 0x00000040,
    X_CREATE_WINDOW_VALUE_MASK_backing_planes        = 0x00000080,
    X_CREATE_WINDOW_VALUE_MASK_backing_pixel         = 0x00000100,
    X_CREATE_WINDOW_VALUE_MASK_override_redirect     = 0x00000200,
    X_CREATE_WINDOW_VALUE_MASK_save_under            = 0x00000400,
    X_CREATE_WINDOW_VALUE_MASK_event_mask            = 0x00000800,
    X_CREATE_WINDOW_VALUE_MASK_do_not_propagate_mask = 0x00001000,
    X_CREATE_WINDOW_VALUE_MASK_colormap              = 0x00002000,
    X_CREATE_WINDOW_VALUE_MASK_cursor                = 0x00004000
} XCreateWindowValueMask;


typedef enum {
    X_CONFIGURE_WINDOW_VALUE_MASK_x            = 0x0001,
    X_CONFIGURE_WINDOW_VALUE_MASK_y            = 0x0002,
    X_CONFIGURE_WINDOW_VALUE_MASK_width        = 0x0004,
    X_CONFIGURE_WINDOW_VALUE_MASK_height       = 0x0008,
    X_CONFIGURE_WINDOW_VALUE_MASK_border_width = 0x0010,
    X_CONFIGURE_WINDOW_VALUE_MASK_sibling      = 0x0020,
    X_CONFIGURE_WINDOW_VALUE_MASK_stack_mode   = 0x0040
} XConfigureWindowValueMask;

typedef struct
{
    guint8 byte_order;
    guint16 protocol_major_version, protocol_minor_version;
    gchar *authorization_protocol_name;
    guint8 *authorization_protocol_data;
    guint16 authorization_protocol_data_length;
} XConnect;

typedef struct
{
    guint8 depth;
    guint32 wid;
    guint32 parent;
    gint16 x;
    gint16 y;
    guint16 width;
    guint16 height;
    guint16 border_width;
    guint16 class;
    guint32 visual;
    guint32 value_mask;
    guint32 background_pixmap;
    guint32 background_pixel;
    guint32 border_pixmap;
    guint32 border_pixel;
    guint8 bit_gravity;
    guint8 win_gravity;
    guint8 backing_store;
    guint32 backing_planes;
    guint32 backing_pixel;
    gboolean override_redirect;
    gboolean save_under;
    guint32 event_mask;
    guint32 do_not_propogate_mask;
    guint32 colormap;
    guint32 cursor;
} XCreateWindow;

typedef struct
{
    guint32 window;
} XDestroyWindow;

typedef struct
{
    guint32 window;
} XMapWindow;

typedef struct
{
    guint32 window;
} XUnmapWindow;

typedef struct
{
    guint32 window;
    guint16 value_mask;
    gint16 x;
    gint16 y;
    guint16 width;
    guint16 height;
    guint16 border_width;
    guint32 sibling;
    guint8 stack_mode;
} XConfigureWindow;

typedef struct
{
    gboolean only_if_exists;
    gchar *name;
} XInternAtom;

typedef struct
{
    gboolean delete;
    guint32 window;
    guint32 property;
    guint32 type;
    guint32 long_offset;
    guint32 long_length;
} XGetProperty;

typedef struct
{
    guint32 cid;
    guint32 drawable;
    guint32 value_mask;
    guint8 function;
    guint32 plane_mask;
    guint32 foreground;
    guint32 background;
    guint16 line_width;
    guint8 line_style;
    guint8 cap_style;
    guint8 join_style;
    guint8 fill_style;
    guint8 fill_rule;
    guint32 tile;
    guint32 stipple;
    guint16 tile_stipple_x_origin;
    guint16 tile_stipple_y_origin;
    guint32 font;
    guint8 subwindow_mode;
    gboolean graphics_exposures;
    guint16 clip_x_origin;
    guint16 clip_y_origin;
    guint32 clip_mask;
    guint16 dash_offset;
    guint8 dashes;
    guint8 arc_mode;
} XCreateGC;

typedef struct
{
    gchar *name;
} XQueryExtension;

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
   void (*create_window)(XClient *client, XCreateWindow *message);
   void (*destroy_window)(XClient *client, XDestroyWindow *message);
   void (*map_window)(XClient *client, XMapWindow *message);
   void (*unmap_window)(XClient *client, XUnmapWindow *message);
   void (*configure_window)(XClient *client, XConfigureWindow *message);
   void (*intern_atom)(XClient *client, XInternAtom *message);
   void (*get_property)(XClient *client, XGetProperty *message);
   void (*create_gc)(XClient *client, XCreateGC *message);
   void (*query_extension)(XClient *client, XQueryExtension *message);
   void (*disconnected)(XClient *client);
} XClientClass;

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

GType x_client_get_type (void);

XServer *x_server_new (gint display_number);

void x_server_set_listen_unix (XServer *server, gboolean listen_unix);

void x_server_set_listen_tcp (XServer *server, gboolean listen_tcp);

gboolean x_server_start (XServer *server);

gsize x_server_get_n_clients (XServer *server);

GInetAddress *x_client_get_address (XClient *client);

void x_client_send_failed (XClient *client, const gchar *reason);

void x_client_send_success (XClient *client);

void x_client_send_query_extension_response (XClient *client, guint16 sequence_number, gboolean present, guint8 major_opcode, guint8 first_event, guint8 first_error);

void x_client_disconnect (XClient *client);

G_END_DECLS

#endif /* _X_SERVER_H_ */
