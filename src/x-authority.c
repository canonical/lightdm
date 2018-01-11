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
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib/gstdio.h>

#include "x-authority.h"

struct XAuthorityPrivate
{
    /* Protocol family */
    guint16 family;

    /* Address of the X server (format dependent on family) */
    guint8 *address;
    gsize address_length;

    /* Display number of X server */
    gchar *number;

    /* Authorization scheme */
    gchar *authorization_name;

    /* Authorization data */
    guint8 *authorization_data;
    gsize authorization_data_length;
};

G_DEFINE_TYPE (XAuthority, x_authority, G_TYPE_OBJECT)

XAuthority *
x_authority_new (guint16 family, const guint8 *address, gsize address_length, const gchar *number, const gchar *name, const guint8 *data, gsize data_length)
{
    XAuthority *auth = g_object_new (X_AUTHORITY_TYPE, NULL);

    x_authority_set_family (auth, family);
    x_authority_set_address (auth, address, address_length);
    x_authority_set_number (auth, number);
    x_authority_set_authorization_name (auth, name);
    x_authority_set_authorization_data (auth, data, data_length);

    return auth;
}

XAuthority *
x_authority_new_cookie (guint16 family, const guint8 *address, gsize address_length, const gchar *number)
{
    guint8 cookie[16];
    for (gint i = 0; i < 16; i++)
        cookie[i] = g_random_int () & 0xFF;

    return x_authority_new (family, address, address_length, number, "MIT-MAGIC-COOKIE-1", cookie, 16);
}

XAuthority *
x_authority_new_local_cookie (const gchar *number)
{
    gchar hostname[1024];
    gethostname (hostname, 1024);
    return x_authority_new_cookie (XAUTH_FAMILY_LOCAL, (guint8 *) hostname, strlen (hostname), number);
}

void
x_authority_set_family (XAuthority *auth, guint16 family)
{
    g_return_if_fail (auth != NULL);
    auth->priv->family = family;
}

guint16
x_authority_get_family (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, 0);
    return auth->priv->family;
}

void
x_authority_set_address (XAuthority *auth, const guint8 *address, gsize address_length)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->address);
    auth->priv->address = g_malloc (address_length);
    memcpy (auth->priv->address, address, address_length);
    auth->priv->address_length = address_length;
}

const guint8 *
x_authority_get_address (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->address;
}

const gsize
x_authority_get_address_length (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, 0);
    return auth->priv->address_length;
}

void
x_authority_set_number (XAuthority *auth, const gchar *number)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->number);
    auth->priv->number = g_strdup (number);
}

const gchar *
x_authority_get_number (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->number;
}

void
x_authority_set_authorization_name (XAuthority *auth, const gchar *name)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->authorization_name);
    auth->priv->authorization_name = g_strdup (name);
}

const gchar *
x_authority_get_authorization_name (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->authorization_name;
}

void
x_authority_set_authorization_data (XAuthority *auth, const guint8 *data, gsize data_length)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->authorization_data);
    auth->priv->authorization_data = g_malloc (data_length);
    memcpy (auth->priv->authorization_data, data, data_length);
    auth->priv->authorization_data_length = data_length;
}

const guint8 *
x_authority_get_authorization_data (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->authorization_data;
}

guint8 *
x_authority_copy_authorization_data (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);

    guint8 *data = g_malloc (auth->priv->authorization_data_length);
    memcpy (data, auth->priv->authorization_data, auth->priv->authorization_data_length);
    return data;
}

gsize
x_authority_get_authorization_data_length (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, 0);
    return auth->priv->authorization_data_length;
}

static gboolean
read_uint16 (gchar *data, gsize data_length, gsize *offset, guint16 *value)
{
    if (data_length - *offset < 2)
        return FALSE;

    *value = data[*offset] << 8 | data[*offset + 1];
    *offset += 2;

    return TRUE;
}

static gboolean
read_data (gchar *data, gsize data_length, gsize *offset, guint16 length, guint8 **value)
{
    g_free (*value);
    *value = NULL;

    if (data_length - *offset < length)
        return FALSE;

    *value = g_malloc0 (length + 1);
    for (int i = 0; i < length; i++)
        (*value)[i] = data[*offset + i];
    *offset += length;
    (*value)[length] = 0;

    return TRUE;
}

static gboolean
read_string (gchar *data, gsize data_length, gsize *offset, gchar **value)
{
    guint16 length;
    if (!read_uint16 (data, data_length, offset, &length))
        return FALSE;
    return read_data (data, data_length, offset, length, (guint8 **) value);
}

static gboolean
write_uint16 (int fd, guint16 value)
{
    guint8 v[2];
    v[0] = value >> 8;
    v[1] = value & 0xFF;
    return write (fd, v, 2) == 2;
}

static gboolean
write_data (int fd, const guint8 *value, gsize value_length)
{
    return write (fd, value, value_length) == value_length;
}

static gboolean
write_string (int fd, const gchar *value)
{
    size_t value_length = strlen (value);
    return write_uint16 (fd, value_length) && write_data (fd, (guint8 *) value, value_length);
}

gboolean
x_authority_write (XAuthority *auth, XAuthWriteMode mode, const gchar *filename, GError **error)
{
    g_return_val_if_fail (auth != NULL, FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);

    /* Read out existing records */
    g_autofree gchar *input = NULL;
    gsize input_length = 0, input_offset = 0;
    if (mode != XAUTH_WRITE_MODE_SET)
    {
        g_autoptr(GError) read_error = NULL;
        g_file_get_contents (filename, &input, &input_length, &read_error);
        if (read_error && !g_error_matches (read_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            g_warning ("Error reading existing Xauthority: %s", read_error->message);
    }
    GList *records = NULL;
    gboolean matched = FALSE;
    while (input_offset != input_length)
    {
        g_autoptr(XAuthority) a = g_object_new (X_AUTHORITY_TYPE, NULL);

        guint16 address_length = 0;
        guint16 authorization_data_length = 0;
        gboolean result = read_uint16 (input, input_length, &input_offset, &a->priv->family) &&
                          read_uint16 (input, input_length, &input_offset, &address_length) &&
                          read_data (input, input_length, &input_offset, address_length, &a->priv->address) &&
                          read_string (input, input_length, &input_offset, &a->priv->number) &&
                          read_string (input, input_length, &input_offset, &a->priv->authorization_name) &&
                          read_uint16 (input, input_length, &input_offset, &authorization_data_length) &&
                          read_data (input, input_length, &input_offset, authorization_data_length, &a->priv->authorization_data);
        a->priv->address_length = address_length;
        a->priv->authorization_data_length = authorization_data_length;

        if (!result)
            break;

        gboolean address_matches = FALSE;
        if (auth->priv->address_length == a->priv->address_length)
        {
            guint16 i;
            for (i = 0; i < auth->priv->address_length && auth->priv->address[i] == a->priv->address[i]; i++);
            address_matches = i == auth->priv->address_length;
        }

        /* If this record matches, then update or delete it */
        if (!matched &&
            auth->priv->family == a->priv->family &&
            address_matches &&
            strcmp (auth->priv->number, a->priv->number) == 0)
        {
            matched = TRUE;
            if (mode == XAUTH_WRITE_MODE_REMOVE)
                continue;
            else
                x_authority_set_authorization_data (a, auth->priv->authorization_data, auth->priv->authorization_data_length);
        }

        records = g_list_append (records, g_steal_pointer (&a));
    }

    /* If didn't exist, then add a new one */
    if (!matched)
        records = g_list_append (records, g_object_ref (auth));

    /* Write records back */
    errno = 0;
    int output_fd = g_open (filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if (output_fd < 0)
    {
        g_set_error (error,
                     G_FILE_ERROR,
                     g_file_error_from_errno (errno),
                     "Failed to open X authority %s: %s",
                     filename,
                     g_strerror (errno));
        return FALSE;
    }

    errno = 0;
    gboolean result = TRUE;
    for (GList *link = records; link && result; link = link->next)
    {
        XAuthority *a = link->data;

        result = write_uint16 (output_fd, a->priv->family) &&
                 write_uint16 (output_fd, a->priv->address_length) &&
                 write_data (output_fd, a->priv->address, a->priv->address_length) &&
                 write_string (output_fd, a->priv->number) &&
                 write_string (output_fd, a->priv->authorization_name) &&
                 write_uint16 (output_fd, a->priv->authorization_data_length) &&
                 write_data (output_fd, a->priv->authorization_data, a->priv->authorization_data_length);
    }
    g_list_free_full (records, g_object_unref);

    fsync (output_fd);
    close (output_fd);

    if (!result)
    {
        g_set_error (error,
                     G_FILE_ERROR,
                     g_file_error_from_errno (errno),
                     "Failed to write X authority %s: %s",
                     filename,
                     g_strerror (errno));
        return FALSE;
    }

    return TRUE;
}

static void
x_authority_init (XAuthority *auth)
{
    auth->priv = G_TYPE_INSTANCE_GET_PRIVATE (auth, X_AUTHORITY_TYPE, XAuthorityPrivate);
    auth->priv->number = g_strdup ("");
}

static void
x_authority_finalize (GObject *object)
{
    XAuthority *self = X_AUTHORITY (object);

    g_clear_pointer (&self->priv->address, g_free);
    g_clear_pointer (&self->priv->number, g_free);
    g_clear_pointer (&self->priv->authorization_name, g_free);
    g_clear_pointer (&self->priv->authorization_data, g_free);

    G_OBJECT_CLASS (x_authority_parent_class)->finalize (object);
}

static void
x_authority_class_init (XAuthorityClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = x_authority_finalize;

    g_type_class_add_private (klass, sizeof (XAuthorityPrivate));
}
