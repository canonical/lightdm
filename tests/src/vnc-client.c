#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <gio/gio.h>

#include "status.h"

static GKeyFile *config;

int
main (int argc, char **argv)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketAddress) address = NULL;
    gboolean result;
    gchar buffer[1024];
    gssize n_read, n_sent;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    status_connect (NULL, NULL);

    status_notify ("VNC-CLIENT START");

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);

    status_notify ("VNC-CLIENT CONNECT");

    socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
    if (!socket)
    {
        g_warning ("Unable to make VNC socket: %s", error->message);
        return EXIT_FAILURE;
    }

    address = g_inet_socket_address_new (g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4), 5900);
    result = g_socket_connect (socket, address, NULL, &error);
    if (!result)
    {
        g_warning ("Unable to connect VNC socket: %s", error->message);
        return EXIT_FAILURE;
    }

    n_read = g_socket_receive (socket, buffer, 1023, NULL, &error);
    if (n_read <= 0)
    {
        g_warning ("Unable to receive on VNC socket: %s", error->message);
        return EXIT_FAILURE;
    }

    buffer[n_read] = '\0';
    if (g_str_has_suffix (buffer, "\n"))
        buffer[n_read-1] = '\0';
    status_notify ("VNC-CLIENT CONNECTED VERSION=\"%s\"", buffer);

    snprintf (buffer, 1024, "RFB 003.003\n");
    n_sent = g_socket_send (socket, buffer, strlen (buffer), NULL, &error);
    if (n_sent != strlen (buffer))
    {
        g_warning ("Unable to send on VNC socket: %s", error->message);
        return EXIT_FAILURE;
    }

    while (TRUE)
    {
        n_read = g_socket_receive (socket, buffer, 1023, NULL, &error);
        if (n_read < 0)
        {
            g_warning ("Unable to receive on VNC socket: %s", error->message);
            return EXIT_FAILURE;
        }

        if (n_read == 0)
        {
            status_notify ("VNC-CLIENT DISCONNECTED");
            return EXIT_SUCCESS;
        }
    }

}
