#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include "status.h"

void
notify_status (const gchar *format, ...)
{
    GSocket *s;
    GSocketAddress *address;
    const gchar *path;
    gchar status[1024];
    va_list ap;
    GError *error = NULL;
  
    va_start (ap, format);
    vsnprintf (status, 1024, format, ap);
    va_end (ap);

    path = g_getenv ("LIGHTDM_TEST_STATUS_SOCKET");
    if (!path)
    {
        static gboolean warned = FALSE;
      
        if (!warned)
        {
            g_printerr ("LIGHTDM_TEST_STATUS_SOCKET not defined\n");
            warned = TRUE;
        }

        g_printerr ("%s\n", status);
        return;
    }

    s = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
    if (error)
        g_printerr ("Unable to open socket for status: %s\n", error->message);
    g_clear_error (&error);
    if (!s)
        return;

    address = g_unix_socket_address_new (path);
    g_socket_connect (s, address, NULL, &error);
    g_object_unref (address);
    if (error)
        g_printerr ("Failed to connect to status socket %s: %s\n", path, error->message);
    g_clear_error (&error);

    g_socket_send (s, status, strlen (status), NULL, &error);
    if (error)
        g_printerr ("Failed to write to status socket: %s\n", error->message);
    g_clear_error (&error);
    g_object_unref (s);
}
