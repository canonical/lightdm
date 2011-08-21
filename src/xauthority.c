/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include "xauthority.h"

struct XAuthorityPrivate
{
    /* Protocol family */
    guint16 family;

    /* Host address of X server */
    gchar *address;
  
    /* Display number of X server */
    gchar *number;

    /* Authorization scheme */
    gchar *authorization_name;

    /* Authorization data */
    guint8 *authorization_data;
    guint16 authorization_data_length;
};

G_DEFINE_TYPE (XAuthority, xauth, G_TYPE_OBJECT);

XAuthority *
xauth_new (guint16 family, const gchar *address, const gchar *number, const gchar *name, const guint8 *data, gsize data_length)
{
    XAuthority *auth = g_object_new (XAUTHORITY_TYPE, NULL);

    xauth_set_family (auth, family);  
    xauth_set_address (auth, address);
    xauth_set_number (auth, number);
    xauth_set_authorization_name (auth, name);
    xauth_set_authorization_data (auth, data, data_length);

    return auth;
}

XAuthority *
xauth_new_cookie (guint16 family, const gchar *address, const gchar *number)
{
    guint8 cookie[16];
    gint i;
  
    for (i = 0; i < 16; i++)
        cookie[i] = g_random_int () & 0xFF;

    return xauth_new (family, address, number, "MIT-MAGIC-COOKIE-1", cookie, 16);
}

void
xauth_set_family (XAuthority *auth, guint16 family)
{
    g_return_if_fail (auth != NULL);
    auth->priv->family = family;
}

guint16
xauth_get_family (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, 0);
    return auth->priv->family;
}

void
xauth_set_address (XAuthority *auth, const gchar *address)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->address);
    auth->priv->address = g_strdup (address);
}

const gchar *
xauth_get_address (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->address;
}

void
xauth_set_number (XAuthority *auth, const gchar *number)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->number);
    auth->priv->number = g_strdup (number);
}

const gchar *
xauth_get_number (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->number;
}

void
xauth_set_authorization_name (XAuthority *auth, const gchar *name)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->authorization_name);
    auth->priv->authorization_name = g_strdup (name);
}

const gchar *
xauth_get_authorization_name (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->authorization_name;
}

void
xauth_set_authorization_data (XAuthority *auth, const guint8 *data, gsize data_length)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->authorization_data);
    auth->priv->authorization_data = g_malloc (data_length);
    memcpy (auth->priv->authorization_data, data, data_length);
    auth->priv->authorization_data_length = data_length;
}

const guint8 *
xauth_get_authorization_data (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->authorization_data;
}

guint8 *
xauth_copy_authorization_data (XAuthority *auth)
{
    guint8 *data;

    g_return_val_if_fail (auth != NULL, NULL);

    data = g_malloc (auth->priv->authorization_data_length);
    memcpy (data, auth->priv->authorization_data, auth->priv->authorization_data_length);
    return data;
}

gsize
xauth_get_authorization_data_length (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, 0);
    return auth->priv->authorization_data_length;
}

static gboolean
read_uint16 (GInputStream *stream, guint16 *value, gboolean *eof, GError **error)
{
    guint8 data[2] = {0, 0};
    gsize n_read;

    if (g_input_stream_read_all (stream, data, 2, &n_read, NULL, error) < 0)
        return FALSE;
  
    if (n_read == 0 && eof)
       *eof = TRUE;

    *value = data[0] << 8 | data[1];
  
    return TRUE;
}

static gboolean
read_data (GInputStream *stream, guint16 length, guint8 **value, GError **error)
{
    *value = g_malloc0 (length + 1);
    if (g_input_stream_read_all (stream, *value, length, NULL, NULL, error) < 0)
    {
        g_free (*value);
        *value = NULL;
        return FALSE;
    }
    (*value)[length] = 0;

    return TRUE;
}

static gboolean
read_string (GInputStream *stream, gchar **value, GError **error)
{
    guint16 length;
    if (!read_uint16 (stream, &length, NULL, error))
        return FALSE;
    return read_data (stream, length, (guint8 **) value, error);
}

static gboolean
write_uint16 (GOutputStream *stream, guint16 value, GError **error)
{
    guint8 data[2];

    data[0] = value >> 8;
    data[1] = value & 0xFF;
    return g_output_stream_write (stream, data, 2, NULL, error) >= 0;
}

static gboolean
write_data (GOutputStream *stream, const guint8 *data, gsize data_length, GError **error)
{
    return g_output_stream_write (stream, data, data_length, NULL, error) >= 0;
}

static gboolean
write_string (GOutputStream *stream, const gchar *value, GError **error)
{
    if (!write_uint16 (stream, strlen (value), error) ||
        !write_data (stream, (guint8 *) value, strlen (value), error))
        return FALSE;
  
    return TRUE;
}

gboolean
xauth_write (XAuthority *auth, XAuthWriteMode mode, User *user, GFile *file, GError **error)
{
    GList *link, *records = NULL;
    GFileInputStream *input_stream = NULL;
    GFileOutputStream *output_stream;
    XAuthority *a;
    gboolean result;
    gboolean matched = FALSE;

    /* Read out existing records */
    if (mode != XAUTH_WRITE_MODE_SET)
    {
        GError *read_error = NULL;

        input_stream = g_file_read (file, NULL, &read_error);
        if (!input_stream && !g_error_matches (read_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            g_warning ("Error reading existing Xauthority: %s", read_error->message);
        g_clear_error (&read_error);
    }
    while (input_stream)
    {
        gboolean eof = FALSE;
        GError *read_error = NULL;

        a = g_object_new (XAUTHORITY_TYPE, NULL);

        result = read_uint16 (G_INPUT_STREAM (input_stream), &a->priv->family, &eof, &read_error) &&
                 read_string (G_INPUT_STREAM (input_stream), &a->priv->address, &read_error) &&
                 read_string (G_INPUT_STREAM (input_stream), &a->priv->number, &read_error) &&
                 read_string (G_INPUT_STREAM (input_stream), &a->priv->authorization_name, &read_error) &&
                 read_uint16 (G_INPUT_STREAM (input_stream), &a->priv->authorization_data_length, NULL, &read_error) &&
                 read_data (G_INPUT_STREAM (input_stream), a->priv->authorization_data_length, &a->priv->authorization_data, &read_error);
        if (!result)
            g_warning ("Error reading X authority %s: %s", g_file_get_path (file), read_error->message);
        g_clear_error (&read_error);

        if (eof || !result)
            break;

        /* If this record matches, then update or delete it */
        if (!matched &&
            auth->priv->family == a->priv->family &&
            strcmp (auth->priv->address, a->priv->address) == 0 &&
            strcmp (auth->priv->number, a->priv->number) == 0)
        {
            matched = TRUE;
            if (mode == XAUTH_WRITE_MODE_REMOVE)
            {
                g_object_unref (a);
                continue;
            }
            else
                xauth_set_authorization_data (a, auth->priv->authorization_data, auth->priv->authorization_data_length);
        }

        records = g_list_append (records, a);
    }
    if (input_stream)
    {
        GError *close_error = NULL;
        if (!g_input_stream_close (G_INPUT_STREAM (input_stream), NULL, &close_error))
            g_warning ("Error closing Xauthority: %s", close_error->message);
        g_clear_error (&close_error);
        g_object_unref (input_stream);
    }

    /* If didn't exist, then add a new one */
    if (!matched)
        records = g_list_append (records, g_object_ref (auth));

    output_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, error);
    if (!output_stream)
        return FALSE;

    /* Workaround because g_file_replace () generates a file does not exist error even though it can replace it */
    g_clear_error (error);

    /* Write records back */
    result = TRUE;
    for (link = records; link && result; link = link->next)
    {
        XAuthority *a = link->data;

        result = write_uint16 (G_OUTPUT_STREAM (output_stream), a->priv->family, error) &&
                 write_string (G_OUTPUT_STREAM (output_stream), a->priv->address, error) &&
                 write_string (G_OUTPUT_STREAM (output_stream), a->priv->number, error) &&
                 write_string (G_OUTPUT_STREAM (output_stream), a->priv->authorization_name, error) &&
                 write_uint16 (G_OUTPUT_STREAM (output_stream), a->priv->authorization_data_length, error) &&
                 write_data (G_OUTPUT_STREAM (output_stream), a->priv->authorization_data, a->priv->authorization_data_length, error);

        g_object_unref (a);
    }
    g_list_free (records);

    if (result)
        result = g_output_stream_close (G_OUTPUT_STREAM (output_stream), NULL, error);
    g_object_unref (output_stream);

    if (!result)
        return FALSE;

    /* NOTE: Would like to do:
     * g_file_set_attribute_string (file, G_FILE_ATTRIBUTE_OWNER_USER, username, G_FILE_QUERY_INFO_NONE, NULL, error))
     * but not supported. */
    if (user && getuid () == 0)
    {
        if (chown (g_file_get_path (file), user_get_uid (user), user_get_gid (user)) < 0)
            g_warning ("Failed to set authorization owner: %s", strerror (errno));
    }
  
    return TRUE;
}

static void
xauth_init (XAuthority *auth)
{
    auth->priv = G_TYPE_INSTANCE_GET_PRIVATE (auth, XAUTHORITY_TYPE, XAuthorityPrivate);
    auth->priv->address = g_strdup ("");
    auth->priv->number = g_strdup ("");
}

static void
xauth_finalize (GObject *object)
{
    XAuthority *self;

    self = XAUTHORITY (object);

    g_free (self->priv->address);
    g_free (self->priv->number);
    g_free (self->priv->authorization_name);
    g_free (self->priv->authorization_data);

    G_OBJECT_CLASS (xauth_parent_class)->finalize (object);  
}

static void
xauth_class_init (XAuthorityClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = xauth_finalize;

    g_type_class_add_private (klass, sizeof (XAuthorityPrivate));
}
