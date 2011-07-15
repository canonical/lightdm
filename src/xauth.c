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

#include "xauth.h"

struct XAuthorizationPrivate
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
    gsize authorization_data_length;
};

G_DEFINE_TYPE (XAuthorization, xauth, G_TYPE_OBJECT);

XAuthorization *
xauth_new (guint16 family, const gchar *address, const gchar *number, const gchar *name, const guint8 *data, gsize data_length)
{
    XAuthorization *auth = g_object_new (XAUTH_TYPE, NULL);

    xauth_set_family (auth, family);  
    xauth_set_address (auth, address);
    xauth_set_number (auth, number);
    xauth_set_authorization_name (auth, name);
    xauth_set_authorization_data (auth, data, data_length);

    return auth;
}

XAuthorization *
xauth_new_cookie (guint16 family, const gchar *address, const gchar *number)
{
    guint8 cookie[16];
    gint i;
  
    for (i = 0; i < 16; i++)
        cookie[i] = g_random_int () & 0xFF;

    return xauth_new (family, address, number, "MIT-MAGIC-COOKIE-1", cookie, 16);
}

void
xauth_set_family (XAuthorization *auth, guint16 family)
{
    g_return_if_fail (auth != NULL);
    auth->priv->family = family;
}

guint16
xauth_get_family (XAuthorization *auth)
{
    g_return_val_if_fail (auth != NULL, 0);
    return auth->priv->family;
}

void
xauth_set_address (XAuthorization *auth, const gchar *address)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->address);
    auth->priv->address = g_strdup (address);
}

const gchar *
xauth_get_address (XAuthorization *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->address;
}

void
xauth_set_number (XAuthorization *auth, const gchar *number)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->number);
    auth->priv->number = g_strdup (number);
}

const gchar *
xauth_get_number (XAuthorization *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->number;
}

void
xauth_set_authorization_name (XAuthorization *auth, const gchar *name)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->authorization_name);
    auth->priv->authorization_name = g_strdup (name);
}

const gchar *
xauth_get_authorization_name (XAuthorization *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->authorization_name;
}

void
xauth_set_authorization_data (XAuthorization *auth, const guint8 *data, gsize data_length)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->authorization_data);
    auth->priv->authorization_data = g_malloc (data_length);
    memcpy (auth->priv->authorization_data, data, data_length);
    auth->priv->authorization_data_length = data_length;
}

const guint8 *
xauth_get_authorization_data (XAuthorization *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->authorization_data;
}

guint8 *
xauth_copy_authorization_data (XAuthorization *auth)
{
    guint8 *data;

    g_return_val_if_fail (auth != NULL, NULL);

    data = g_malloc (auth->priv->authorization_data_length);
    memcpy (data, auth->priv->authorization_data, auth->priv->authorization_data_length);
    return data;
}

gsize
xauth_get_authorization_data_length (XAuthorization *auth)
{
    g_return_val_if_fail (auth != NULL, 0);
    return auth->priv->authorization_data_length;
}

static guint16
read_uint16 (GInputStream *stream, gboolean *eof, GError **error)
{
    guint8 data[2] = {0, 0};
    gsize n_read;

    if (error && *error)
        return 0;
  
    if (g_input_stream_read_all (stream, data, 2, &n_read, NULL, error) < 0)
        return 0;
  
    if (n_read == 0 && eof)
       *eof = TRUE;

    return data[0] << 8 | data[1];
}

static guint8 *
read_data (GInputStream *stream, guint16 length, GError **error)
{
    guint8 *data;
  
    if (error && *error)
        return NULL;

    data = g_malloc0 (length + 1);
    if (g_input_stream_read_all (stream, data, length, NULL, NULL, error) < 0)
    {
        g_free (data);
        return NULL;
    }
    data[length] = 0;

    return data;
}

static gchar *
read_string (GInputStream *stream, GError **error)
{
    guint16 length;
    length = read_uint16 (stream, NULL, error);
    return (gchar *) read_data (stream, length, error);
}

static void
write_uint16 (GOutputStream *stream, guint16 value, GError **error)
{
    guint8 data[2];

    if (error && *error)
        return;

    data[0] = value >> 8;
    data[1] = value & 0xFF;
    g_output_stream_write (stream, data, 2, NULL, error);
}

static void
write_data (GOutputStream *stream, const guint8 *data, gsize data_length, GError **error)
{
    if (error && *error)
        return;
    g_output_stream_write (stream, data, data_length, NULL, error);
}

static void
write_string (GOutputStream *stream, const gchar *value, GError **error)
{
    write_uint16 (stream, strlen (value), error);
    write_data (stream, (guint8 *) value, strlen (value), error);
}

gboolean
xauth_write (XAuthorization *auth, XAuthWriteMode mode, User *user, GFile *file, GError **error)
{
    GList *link, *records = NULL;
    GFileInputStream *input_stream = NULL;
    GFileOutputStream *output_stream;
    XAuthorization *a;
    gboolean matched = FALSE;
  
    /* Read out existing records */
    if (mode != XAUTH_WRITE_MODE_SET)
    {
        input_stream = g_file_read (file, NULL, error);
        if (!input_stream && error && !g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return FALSE;
    }

    while (input_stream)
    {
        guint16 family;
        gboolean eof = FALSE;

        family = read_uint16 (G_INPUT_STREAM (input_stream), &eof, error);
        if (eof)
            break;

        a = g_object_new (XAUTH_TYPE, NULL);
        a->priv->family = family;
        a->priv->address = read_string (G_INPUT_STREAM (input_stream), error);
        a->priv->number = read_string (G_INPUT_STREAM (input_stream), error);
        a->priv->authorization_name = read_string (G_INPUT_STREAM (input_stream), error);
        a->priv->authorization_data_length = read_uint16 (G_INPUT_STREAM (input_stream), NULL, error);
        a->priv->authorization_data = read_data (G_INPUT_STREAM (input_stream), a->priv->authorization_data_length, error);

        if (error && *error)
        {
            g_warning ("Error reading X authority %s: %s", g_file_get_path (file), (*error)->message);
            g_clear_error (error);
            break;
        }

        /* If this record matches, then update or delete it */
        if (!matched &&
            auth->priv->family == family &&
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
        g_input_stream_close (G_INPUT_STREAM (input_stream), NULL, error);
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
    for (link = records; link; link = link->next)
    {
        XAuthorization *a = link->data;

        write_uint16 (G_OUTPUT_STREAM (output_stream), a->priv->family, error);
        write_string (G_OUTPUT_STREAM (output_stream), a->priv->address, error);
        write_string (G_OUTPUT_STREAM (output_stream), a->priv->number, error);
        write_string (G_OUTPUT_STREAM (output_stream), a->priv->authorization_name, error);
        write_uint16 (G_OUTPUT_STREAM (output_stream), a->priv->authorization_data_length, error);
        write_data (G_OUTPUT_STREAM (output_stream), a->priv->authorization_data, a->priv->authorization_data_length, error);

        g_object_unref (a);
    }
    g_list_free (records);
    g_output_stream_close (G_OUTPUT_STREAM (output_stream), NULL, error);
    g_object_unref (output_stream);

    if (error && *error)
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
xauth_init (XAuthorization *auth)
{
    auth->priv = G_TYPE_INSTANCE_GET_PRIVATE (auth, XAUTH_TYPE, XAuthorizationPrivate);
    auth->priv->address = g_strdup ("");
    auth->priv->number = g_strdup ("");
}

static void
xauth_finalize (GObject *object)
{
    XAuthorization *self;

    self = XAUTH (object);

    g_free (self->priv->address);
    g_free (self->priv->number);
    g_free (self->priv->authorization_name);
    g_free (self->priv->authorization_data);

    G_OBJECT_CLASS (xauth_parent_class)->finalize (object);  
}

static void
xauth_class_init (XAuthorizationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = xauth_finalize;  

    g_type_class_add_private (klass, sizeof (XAuthorizationPrivate));
}
