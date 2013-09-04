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

#include "xauthority.h"

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

G_DEFINE_TYPE (XAuthority, xauth, G_TYPE_OBJECT);

XAuthority *
xauth_new (guint16 family, const guint8 *address, gsize address_length, const gchar *number, const gchar *name, const guint8 *data, gsize data_length)
{
    XAuthority *auth = g_object_new (XAUTHORITY_TYPE, NULL);

    xauth_set_family (auth, family);  
    xauth_set_address (auth, address, address_length);
    xauth_set_number (auth, number);
    xauth_set_authorization_name (auth, name);
    xauth_set_authorization_data (auth, data, data_length);

    return auth;
}

XAuthority *
xauth_new_cookie (guint16 family, const guint8 *address, gsize address_length, const gchar *number)
{
    guint8 cookie[16];
    gint i;
  
    for (i = 0; i < 16; i++)
        cookie[i] = g_random_int () & 0xFF;

    return xauth_new (family, address, address_length, number, "MIT-MAGIC-COOKIE-1", cookie, 16);
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
xauth_set_address (XAuthority *auth, const guint8 *address, gsize address_length)
{
    g_return_if_fail (auth != NULL);
    g_free (auth->priv->address);
    auth->priv->address = g_malloc (address_length);
    memcpy (auth->priv->address, address, address_length);
    auth->priv->address_length = address_length;
}

const guint8 *
xauth_get_address (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, NULL);
    return auth->priv->address;
}

const gsize
xauth_get_address_length (XAuthority *auth)
{
    g_return_val_if_fail (auth != NULL, 0);
    return auth->priv->address_length;
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
    int i;

    g_free (*value);
    *value = NULL;

    if (data_length - *offset < length)
        return FALSE;
  
    *value = g_malloc0 (length + 1);
    for (i = 0; i < length; i++)
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

static void
write_uint16 (FILE *file, guint16 value)
{
    guint8 v[2];
    v[0] = value >> 8;
    v[1] = value & 0xFF;
    fwrite (v, 2, 1, file);
}

static void
write_data (FILE *file, const guint8 *value, gsize value_length)
{
    fwrite (value, value_length, 1, file);
}

static void
write_string (FILE *file, const gchar *value)
{
    write_uint16 (file, strlen (value));
    write_data (file, (guint8 *) value, strlen (value));
}

gboolean
xauth_write (XAuthority *auth, XAuthWriteMode mode, const gchar *filename, GError **error)
{
    gchar *input;
    gsize input_length = 0, input_offset = 0;
    GList *link, *records = NULL;
    XAuthority *a;
    gboolean result;
    gboolean matched = FALSE;
    FILE *output;

    g_return_val_if_fail (auth != NULL, FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);

    /* Read out existing records */
    if (mode != XAUTH_WRITE_MODE_SET)
    {
        GError *read_error = NULL;

        g_file_get_contents (filename, &input, &input_length, &read_error);
        if (read_error && !g_error_matches (read_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            g_warning ("Error reading existing Xauthority: %s", read_error->message);
        g_clear_error (&read_error);
    }
    while (input_offset != input_length)
    {
        gboolean address_matches = FALSE;
        guint16 address_length = 0;
        guint16 authorization_data_length = 0;

        a = g_object_new (XAUTHORITY_TYPE, NULL);

        result = read_uint16 (input, input_length, &input_offset, &a->priv->family) &&
                 read_uint16 (input, input_length, &input_offset, &address_length) &&
                 read_data (input, input_length, &input_offset, address_length, &a->priv->address) &&
                 read_string (input, input_length, &input_offset, &a->priv->number) &&
                 read_string (input, input_length, &input_offset, &a->priv->authorization_name) &&
                 read_uint16 (input, input_length, &input_offset, &authorization_data_length) &&
                 read_data (input, input_length, &input_offset, authorization_data_length, &a->priv->authorization_data);
        a->priv->address_length = address_length;
        a->priv->authorization_data_length = authorization_data_length;

        if (!result)
        {
            g_object_unref (a);
            break;
        }

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
            {
                g_object_unref (a);
                continue;
            }
            else
                xauth_set_authorization_data (a, auth->priv->authorization_data, auth->priv->authorization_data_length);
        }

        records = g_list_append (records, a);
    }
    g_free (input);

    /* If didn't exist, then add a new one */
    if (!matched)
        records = g_list_append (records, g_object_ref (auth));

    /* Write records back */
    errno = 0;
    output = fopen (filename, "w");
    if (output == NULL)
    {
        g_set_error (error,
                     G_FILE_ERROR,
                     g_file_error_from_errno (errno),
                     "Failed to write X authority %s: %s",
                     filename,
                     g_strerror (errno));
        return FALSE;
    }
  
    for (link = records; link && result; link = link->next)
    {
        XAuthority *a = link->data;

        write_uint16 (output, a->priv->family);
        write_uint16 (output, a->priv->address_length);
        write_data (output, a->priv->address, a->priv->address_length);
        write_string (output, a->priv->number);
        write_string (output, a->priv->authorization_name);
        write_uint16 (output, a->priv->authorization_data_length);
        write_data (output, a->priv->authorization_data, a->priv->authorization_data_length);

        g_object_unref (a);
    }
    g_list_free (records);

    fclose (output);

    return TRUE;
}    

static void
xauth_init (XAuthority *auth)
{
    auth->priv = G_TYPE_INSTANCE_GET_PRIVATE (auth, XAUTHORITY_TYPE, XAuthorityPrivate);
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
