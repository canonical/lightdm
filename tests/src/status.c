#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <unistd.h>

#include "status.h"

static GSocket *status_socket = NULL;
static StatusRequestFunc request_func = NULL;

static gboolean
status_request_cb (GSocket *socket, GIOCondition condition, gpointer data)
{
    int length;
    gchar buffer[1024];
    ssize_t n_read;
    GError *error = NULL;  

    n_read = g_socket_receive (socket, (gchar *)&length, sizeof (length), NULL, &error);
    if (n_read > 0)
        n_read = g_socket_receive (socket, buffer, length, NULL, &error);
    if (n_read == 0)
    {
        if (request_func)
            request_func (NULL);
        return FALSE;
    }
    if (error)
        g_warning ("Error reading from socket: %s", error->message);
    g_clear_error (&error);

    if (n_read > 0 && request_func)
    {
        buffer[n_read] = '\0';
        request_func (buffer);
    }

    return TRUE;
}

void
status_connect (StatusRequestFunc request_cb)
{
    gchar *path;
    GSocketAddress *address;
    gboolean result;
    GSource *source;
    GError *error = NULL;

    request_func = request_cb;

    status_socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
    if (error)
        g_printerr ("Unable to open socket for status: %s\n", error->message);
    g_clear_error (&error);
    if (!status_socket)
        return;

    path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), ".s", NULL);
    address = g_unix_socket_address_new (path);
    result = g_socket_connect (status_socket, address, NULL, &error);
    g_object_unref (address);
    if (error)
        g_printerr ("Failed to connect to status socket %s: %s\n", path, error->message);
    g_clear_error (&error);
    g_free (path);
    if (!result)
        return;

    source = g_socket_create_source (status_socket, G_IO_IN, NULL);
    g_source_set_callback (source, (GSourceFunc) status_request_cb, NULL, NULL);
    g_source_attach (source, NULL);   
}

void
status_notify (const gchar *format, ...)
{
    gchar status[1024];
    va_list ap;

    va_start (ap, format);
    vsnprintf (status, 1024, format, ap);
    va_end (ap);

    if (status_socket)
    {
        GError *error = NULL;
        int length;

        length = strlen (status);
        g_socket_send (status_socket, (gchar *) &length, sizeof (length), NULL, &error);
        g_socket_send (status_socket, status, strlen (status), NULL, &error);
        if (error)
            g_printerr ("Failed to write to status socket: %s\n", error->message);
        g_clear_error (&error);
    }
    else
        g_printerr ("%s\n", status);
}
