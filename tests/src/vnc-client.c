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
    gchar *server_address;
    gchar *hostname, *c;
    gint port;
    GError *error = NULL;
    GSocket *socket;
    GSocketConnectable *address;
    GSocketAddress *socket_address;
    gboolean result;
    gchar buffer[1024];
    gssize n_read, n_sent;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    status_connect (NULL);

    status_notify ("VNC-CLIENT START");

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);
  
    if (argc > 1)
        server_address = g_strdup (argv[1]);
    else
        server_address = g_strdup (":0");

    status_notify ("VNC-CLIENT CONNECT SERVER=%s", server_address);

    socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
    if (error)
        g_warning ("Unable to make VNC socket: %s", error->message);
    g_clear_error (&error);
    if (!socket)
        return EXIT_FAILURE;

    hostname = g_strdup (server_address);
    c = strchr (hostname, ':');
    if (c)
    {
        *c = '\0';
        gchar *port_string = c + 1;
        if (g_str_has_prefix (port_string, ":"))
            port = atoi (port_string + 1);
        else
            port = 5900 + atoi (port_string);        
    }
    else
        port = 5900;
    if (strcmp (hostname, "") == 0)
    {
        g_free (hostname);
        hostname = g_strdup ("localhost");
    }
  
    address = g_network_address_new (hostname, port);
    socket_address = g_socket_address_enumerator_next (g_socket_connectable_enumerate (address), NULL, NULL);

    result = g_socket_connect (socket, socket_address, NULL, &error);
    if (error)
        g_warning ("Unable to connect VNC socket: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return EXIT_FAILURE;

    n_read = g_socket_receive (socket, buffer, 1023, NULL, &error);
    if (error)
        g_warning ("Unable to receive on VNC socket: %s", error->message);
    g_clear_error (&error);
    if (n_read <= 0)
        return EXIT_FAILURE;

    buffer[n_read] = '\0';
    if (g_str_has_suffix (buffer, "\n"))
        buffer[n_read-1] = '\0';
    status_notify ("VNC-CLIENT CONNECTED VERSION=\"%s\"", buffer);

    snprintf (buffer, 1024, "RFB 003.003\n");
    n_sent = g_socket_send (socket, buffer, strlen (buffer), NULL, &error);
    if (error)
        g_warning ("Unable to send on VNC socket: %s", error->message);
    g_clear_error (&error);
    if (n_sent != strlen (buffer))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
