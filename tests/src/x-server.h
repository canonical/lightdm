#ifndef X_SERVER_H_
#define X_SERVER_H_

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define X_CLIENT_SIGNAL_DISCONNECTED "disconnected"

#define X_SERVER_SIGNAL_CLIENT_CONNECTED    "client-connected"
#define X_SERVER_SIGNAL_CLIENT_DISCONNECTED "client-disconnected"
#define X_SERVER_SIGNAL_RESET               "reset"

typedef struct XClientPrivate XClientPrivate;

typedef struct
{
   GObject         parent_instance;
   XClientPrivate *priv;
} XClient;

typedef struct
{
   GObjectClass parent_class;
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
   void (*reset)(XServer *server);
} XServerClass;

GType x_server_get_type (void);

XServer *x_server_new (gint display_number);

gboolean x_server_start (XServer *server);

gsize x_server_get_n_clients (XServer *server);

GType x_client_get_type (void);

void x_client_send_failed (XClient *client, const gchar *reason);

void x_client_send_success (XClient *client);

void x_client_send_error (XClient *client, int type, int major, int minor);

void x_client_disconnect (XClient *client);

G_END_DECLS

#endif /* X_SERVER_H_ */
