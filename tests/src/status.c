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
    gchar buffer[1024];
    ssize_t n_read;
    const gchar *c, *start;
    int l;
    gchar *id, *name = NULL;
    gboolean id_matches;
    GHashTable *params;
    GError *error = NULL;  

    n_read = g_socket_receive (socket, (gchar *)&length, sizeof (length), NULL, &error);
    if (n_read > 0)
        n_read = g_socket_receive (socket, buffer, length, NULL, &error);
    if (n_read == 0)
    {
        if (request_func)
            request_func (NULL, NULL);
        return FALSE;
    }
    if (error)
        g_warning ("Error reading from socket: %s", error->message);
    g_clear_error (&error);

    if (n_read <= 0 || !request_func)
        return TRUE;

    buffer[n_read] = '\0';
    c = buffer;
    start = c;
    l = 0;
    while (*c && !isspace (*c))
    {
        c++;
        l++;
    }
    id = g_strdup_printf ("%.*s", l, start);
    id_matches = g_strcmp0 (id, filter_id) == 0;
    g_free (id);
    if (!id_matches)
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
    name = g_strdup_printf ("%.*s", l, start);

    params = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    while (TRUE)
    {
        const gchar *start;
        gchar *param_name, *param_value;

        while (isspace (*c))
            c++;
        start = c;
        while (*c && !isspace (*c) && *c != '=')
            c++;
        if (*c == '\0')
            break;

        param_name = g_strdup_printf ("%.*s", (int) (c - start), start);

        if (*c == '=')
        {
            c++;
            while (isspace (*c))
                c++;
            if (*c == '\"')
            {
                gboolean escaped = FALSE;
                GString *value;

                c++;
                value = g_string_new ("");
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
                param_value = value->str;
                g_string_free (value, FALSE);
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

        g_hash_table_insert (params, param_name, param_value);
    }

    request_func (name, params);

    g_free (name);
    g_hash_table_unref (params);

    return TRUE;
}

gboolean
status_connect (StatusRequestFunc request_cb, const gchar *id)
{
    gchar *path;
    GSocketAddress *address;
    gboolean result;
    GSource *source;
    GError *error = NULL;

    request_func = request_cb;
    filter_id = g_strdup (id);

    status_socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
    if (error)
        g_printerr ("Unable to open socket for status: %s\n", error->message);
    g_clear_error (&error);
    if (!status_socket)
        return FALSE;

    path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), ".s", NULL);
    address = g_unix_socket_address_new (path);
    result = g_socket_connect (status_socket, address, NULL, &error);
    g_object_unref (address);
    if (error)
        g_printerr ("Failed to connect to status socket %s: %s\n", path, error->message);
    g_clear_error (&error);
    g_free (path);
    if (!result)
    {
        g_object_unref (status_socket);
        status_socket = NULL;
        return FALSE;
    }

    source = g_socket_create_source (status_socket, G_IO_IN, NULL);
    g_source_set_callback (source, (GSourceFunc) status_request_cb, NULL, NULL);
    g_source_attach (source, NULL);

    return TRUE;
}

void
status_notify (const gchar *format, ...)
{
    gboolean written = FALSE;

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
        written = g_socket_send (status_socket, (gchar *) &length, sizeof (length), NULL, &error) == sizeof (length) &&
                  g_socket_send (status_socket, status, strlen (status), NULL, &error) == strlen (status);
        if (error)
            g_printerr ("Failed to write to status socket: %s\n", error->message);
        g_clear_error (&error);
    }

    if (!written)
        g_printerr ("%s\n", status);
}
