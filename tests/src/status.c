#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <unistd.h>
#include <ctype.h>

#include "status.h"

static GSocket *status_socket = NULL;
static StatusRequestFunc request_func = NULL;
static gchar *filter_id = NULL;

static gboolean
status_request_cb (GSocket *socket, GIOCondition condition, gpointer data)
{
    int length;
    g_autoptr(GError) error = NULL;
    ssize_t n_read = g_socket_receive (socket, (gchar *)&length, sizeof (length), NULL, &error);
    gchar buffer[1024];
    if (n_read > 0)
        n_read = g_socket_receive (socket, buffer, length, NULL, &error);
    if (error)
    {
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED))
            n_read = 0;
        else
            g_warning ("Error reading from socket: %s", error->message);
    }
    if (n_read == 0)
    {
        if (request_func)
            request_func (NULL, NULL);
        return FALSE;
    }

    if (n_read <= 0 || !request_func)
        return TRUE;

    buffer[n_read] = '\0';
    const gchar *c = buffer;
    const gchar *start = c;
    int l = 0;
    while (*c && !isspace (*c))
    {
        c++;
        l++;
    }
    g_autofree gchar *id = g_strdup_printf ("%.*s", l, start);
    if (g_strcmp0 (id, filter_id) != 0)
        return TRUE;

    while (isspace (*c))
        c++;
    start = c;
    l = 0;
    while (*c && !isspace (*c))
    {
        c++;
        l++;
    }
    g_autofree gchar *name = g_strdup_printf ("%.*s", l, start);

    g_autoptr(GHashTable) params = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    while (TRUE)
    {
        while (isspace (*c))
            c++;
        const gchar *start = c;
        while (*c && !isspace (*c) && *c != '=')
            c++;
        if (*c == '\0')
            break;

        g_autofree gchar *param_name = g_strdup_printf ("%.*s", (int) (c - start), start);
        g_autofree gchar *param_value = NULL;

        if (*c == '=')
        {
            c++;
            while (isspace (*c))
                c++;
            if (*c == '\"')
            {
                c++;
                g_autoptr(GString) value = g_string_new ("");
                gboolean escaped = FALSE;
                while (*c)
                {
                    if (*c == '\\')
                    {
                        if (escaped)
                        {
                            g_string_append_c (value, '\\');
                            escaped = FALSE;
                        }
                        else
                            escaped = TRUE;
                    }
                    else if (!escaped && *c == '\"')
                        break;
                    if (!escaped)
                        g_string_append_c (value, *c);
                    c++;
                }
                param_value = g_strdup (value->str);
                if (*c == '\"')
                    c++;
            }
            else
            {
                start = c;
                while (*c && !isspace (*c))
                    c++;
                param_value = g_strdup_printf ("%.*s", (int) (c - start), start);
            }
        }
        else
            param_value = g_strdup ("");

        g_hash_table_insert (params, g_steal_pointer (&param_name), g_steal_pointer (&param_value));
    }

    request_func (name, params);

    return TRUE;
}

gboolean
status_connect (StatusRequestFunc request_cb, const gchar *id)
{
    request_func = request_cb;
    filter_id = g_strdup (id);

    g_autoptr(GError) error = NULL;
    status_socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
    if (!status_socket)
    {
        g_printerr ("Unable to open socket for status: %s\n", error->message);
        return FALSE;
    }

    g_autofree gchar *path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), ".s", NULL);
    g_autoptr(GSocketAddress) address = g_unix_socket_address_new (path);
    if (!g_socket_connect (status_socket, address, NULL, &error))
    {
        g_printerr ("Failed to connect to status socket %s: %s\n", path, error->message);
        g_clear_object (&status_socket);
        return FALSE;
    }

    GSource *source = g_socket_create_source (status_socket, G_IO_IN, NULL);
    g_source_set_callback (source, (GSourceFunc) status_request_cb, NULL, NULL);
    g_source_attach (source, NULL);

    return TRUE;
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
        int length = strlen (status);
        g_autoptr(GError) error = NULL;
        if (g_socket_send (status_socket, (gchar *) &length, sizeof (length), NULL, &error) < 0 ||
            g_socket_send (status_socket, status, strlen (status), NULL, &error) < 0)
            g_printerr ("Failed to write to status socket: %s\n", error->message);
        else
            return;
    }

    g_printerr ("%s\n", status);
}
