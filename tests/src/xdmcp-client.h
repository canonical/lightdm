#ifndef XDMCP_CLIENT_H_
#define XDMCP_CLIENT_H_

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define XDMCP_CLIENT_TYPE (xdmcp_client_get_type())
#define XDMCP_CLIENT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XDMCP_CLIENT_TYPE, XDMCPClient))

#define XDMCP_VERSION 1
#define XDMCP_PORT 177

#define XDMCP_CLIENT_SIGNAL_WILLING    "willing"
#define XDMCP_CLIENT_SIGNAL_UNWILLING  "unwilling"
#define XDMCP_CLIENT_SIGNAL_ACCEPT     "accept"
#define XDMCP_CLIENT_SIGNAL_DECLINE    "decline"
#define XDMCP_CLIENT_SIGNAL_FAILED     "failed"
#define XDMCP_CLIENT_SIGNAL_ALIVE      "alive"

typedef struct
{
    gchar *authentication_name;
    gchar *hostname;
    gchar *status;
} XDMCPWilling;

typedef struct
{
    gchar *hostname;
    gchar *status;
} XDMCPUnwilling;

typedef struct
{
    guint32 session_id;
    gchar *authentication_name;
    guint16 authentication_data_length;
    guint8 *authentication_data;
    gchar *authorization_name;
    guint16 authorization_data_length;
    guint8 *authorization_data;
} XDMCPAccept;

typedef struct
{
    gchar *status;
    gchar *authentication_name;
    guint16 authentication_data_length;
    guint8 *authentication_data;
} XDMCPDecline;

typedef struct
{
    guint32 session_id;
    gchar *status;
} XDMCPFailed;

typedef struct
{
    gboolean session_running;
    guint32 session_id;
} XDMCPAlive;

typedef struct
{
   GObject parent_instance;
} XDMCPClient;

typedef struct
{
   GObjectClass parent_class;
   void (*willing)(XDMCPClient *client, XDMCPWilling *message);
   void (*unwilling)(XDMCPClient *client, XDMCPUnwilling *message);  
   void (*accept)(XDMCPClient *client, XDMCPAccept *message);
   void (*decline)(XDMCPClient *client, XDMCPDecline *message);
   void (*failed)(XDMCPClient *client, XDMCPFailed *message);
   void (*alive)(XDMCPClient *client, XDMCPAlive *message);
} XDMCPClientClass;

GType xdmcp_client_get_type (void);

XDMCPClient *xdmcp_client_new (void);

void xdmcp_client_set_hostname (XDMCPClient *client, const gchar *hostname);

void xdmcp_client_set_port (XDMCPClient *client, guint16 port);

gboolean xdmcp_client_start (XDMCPClient *client);

GInetAddress *xdmcp_client_get_local_address (XDMCPClient *client);

void xdmcp_client_send_query (XDMCPClient *client, gchar **authentication_names);

void xdmcp_client_send_broadcast_query (XDMCPClient *client, gchar **authentication_names);

void xdmcp_client_send_indirect_query (XDMCPClient *client, gchar **authentication_names);

void xdmcp_client_send_request (XDMCPClient *client,
                                guint16 display_number,
                                GInetAddress **addresses,
                                const gchar *authentication_name,
                                const guint8 *authentication_data, guint16 authentication_data_length,
                                gchar **authorization_names, const gchar *mfid);

void xdmcp_client_send_manage (XDMCPClient *client, guint32 session_id, guint16 display_number, const gchar *display_class);

void xdmcp_client_send_keep_alive (XDMCPClient *client, guint16 display_number, guint32 session_id);

G_END_DECLS

#endif /* XDMCP_CLIENT_H_ */
