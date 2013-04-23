#ifndef XDMCP_CLIENT_H_
#define XDMCP_CLIENT_H_

#include <glib-object.h>
#include <gio/gio.h>

#define XDMCP_VERSION 1
#define XDMCP_PORT 177

typedef struct
{
    gchar *authentication_name;
    gchar *hostname;
    gchar *status;
} XDMCPWilling;

typedef struct
{
    guint32 session_id;
    gchar *authentication_name;
    gchar *authorization_name;
    guint16 authorization_data_length;
    guint8 *authorization_data;
} XDMCPAccept;

typedef struct
{
    gchar *status;
    gchar *authentication_name;
} XDMCPDecline;

typedef struct
{
    guint32 session_id;
    gchar *status;
} XDMCPFailed;

typedef struct XDMCPClientPrivate XDMCPClientPrivate;

typedef struct
{
   GObject             parent_instance;
   XDMCPClientPrivate *priv;
} XDMCPClient;

typedef struct
{
   GObjectClass parent_class;
   void (*query)(XDMCPClient *client);
   void (*willing)(XDMCPClient *client, XDMCPWilling *message);
   void (*accept)(XDMCPClient *client, XDMCPAccept *message);
   void (*decline)(XDMCPClient *client, XDMCPDecline *message);
   void (*failed)(XDMCPClient *client, XDMCPFailed *message);
} XDMCPClientClass;

GType xdmcp_client_get_type (void);

XDMCPClient *xdmcp_client_new (void);

void xdmcp_client_set_hostname (XDMCPClient *client, const gchar *hostname);

void xdmcp_client_set_port (XDMCPClient *client, guint16 port);

gboolean xdmcp_client_start (XDMCPClient *client);

GInetAddress *xdmcp_client_get_local_address (XDMCPClient *client);

void xdmcp_client_send_query (XDMCPClient *client);

void xdmcp_client_send_request (XDMCPClient *client,
                                guint16 display_number,
                                GInetAddress **addresses,
                                const gchar *authentication_name,
                                const guint8 *authentication_data, guint16 authentication_data_length,
                                gchar **authorization_names, const gchar *mfid);

void xdmcp_client_send_manage (XDMCPClient *client, guint32 session_id, guint16 display_number, gchar *display_class);

G_END_DECLS

#endif /* XDMCP_CLIENT_H_ */
