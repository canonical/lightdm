/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>

#include "xauth.h"

struct XAuthorizationPrivate
{
    /* User who is using this authorization */
    gchar *username;

    /* Authorization scheme */
    gchar *authorization_name;

    /* Authorization data */
    guchar *authorization_data;
    gsize authorization_data_length;
};

G_DEFINE_TYPE (XAuthorization, xauth, G_TYPE_OBJECT);

XAuthorization *
xauth_new (const gchar *name, const guchar *data, gsize data_length)
{
    XAuthorization *auth = g_object_new (XAUTH_TYPE, NULL);

    auth->priv->authorization_name = g_strdup (name);
    auth->priv->authorization_data = g_malloc (data_length);
    auth->priv->authorization_data_length = data_length;
    memcpy (auth->priv->authorization_data, data, data_length);

    return auth;
}

XAuthorization *xauth_new_cookie (void)
{
    guchar cookie[16];
    gint i;
  
    for (i = 0; i < 16; i++)
        cookie[i] = g_random_int () & 0xFF;

    return xauth_new ("MIT-MAGIC-COOKIE-1", cookie, 16);
}

const gchar *
xauth_get_authorization_name (XAuthorization *auth)
{
    return auth->priv->authorization_name;
}

const guchar *
xauth_get_authorization_data (XAuthorization *auth)
{
    return auth->priv->authorization_data;
}

gsize
xauth_get_authorization_data_length (XAuthorization *auth)
{
    return auth->priv->authorization_data_length;
}

static void
write_uint16 (GString *string, guint16 value)
{
    g_string_append_c (string, (gchar) (value >> 8));
    g_string_append_c (string, (gchar) value);
}

static void
write_data (GString *string, guchar *value, gsize value_len)
{
    g_string_append_len (string, (gchar *) value, value_len);
}

static void
write_string (GString *string, const gchar *value)
{
    write_uint16 (string, strlen (value));
    g_string_append (string, value);
}

GFile *
xauth_write (XAuthorization *auth, const gchar *username, const gchar *path, GError **error)
{
    GFile *file;
    GFileOutputStream *stream;
    GString *data;
    gboolean result;
    gsize n_written;

    file = g_file_new_for_path (path);

    stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, error);
    if (!stream)
    {
        g_object_unref (file);
        return FALSE;
    }
    
    /* NOTE: Would like to do:
     * g_file_set_attribute_string (file, G_FILE_ATTRIBUTE_OWNER_USER, username, G_FILE_QUERY_INFO_NONE, NULL, error))
     * but not supported. */
    if (username)
    {
        int result = -1;
        struct passwd *info;

        info = getpwnam (username);
        if (info)
            result = chown (path, info->pw_uid, info->pw_gid);

        if (result != 0)
            g_warning ("Failed to set authorization owner");
    }

    data = g_string_sized_new (1024);
    write_uint16 (data, 0xFFFF); /* FamilyWild - this entry is used for all connections */
    write_string (data, ""); /* Not requires as using FamilyWild */
    write_string (data, ""); /* Not requires as using FamilyWild */
    write_string (data, auth->priv->authorization_name);
    write_uint16 (data, auth->priv->authorization_data_length);
    write_data (data, auth->priv->authorization_data, auth->priv->authorization_data_length);

    result = g_output_stream_write_all (G_OUTPUT_STREAM (stream), data->str, data->len, &n_written, NULL, error);
    g_string_free (data, TRUE);

    g_object_unref (stream);
    if (!result)
    {
        g_object_unref (file);
        file = NULL;
    }
  
    return file;
}

static void
xauth_init (XAuthorization *auth)
{
    auth->priv = G_TYPE_INSTANCE_GET_PRIVATE (auth, XAUTH_TYPE, XAuthorizationPrivate);
}

static void
xauth_finalize (GObject *object)
{
    XAuthorization *self;

    self = XAUTH (object);

    g_free (self->priv->username);
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
