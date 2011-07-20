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

#include <config.h>

#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "xserver.h"
#include "configuration.h"
#include "xsession.h"

struct XServerPrivate
{  
    /* Host running the server */
    gchar *hostname;

    /* Display number */
    guint number;

    /* Cached server address */
    gchar *address;

    /* Auhentication scheme to use */
    gchar *authentication_name;

    /* Auhentication data */  
    guint8 *authentication_data;
    gsize authentication_data_length;

    /* Authorization */
    XAuthorization *authorization;

    /* Authority file */
    GFile *authority_file;
};

G_DEFINE_TYPE (XServer, xserver, DISPLAY_SERVER_TYPE);

void
xserver_set_hostname (XServer *server, const gchar *hostname)
{
    g_return_if_fail (server != NULL);
    g_free (server->priv->hostname);
    server->priv->hostname = g_strdup (hostname);
    g_free (server->priv->address);
    server->priv->address = NULL;
}

gchar *
xserver_get_hostname (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->hostname;
}

void
xserver_set_display_number (XServer *server, guint number)
{
    g_return_if_fail (server != NULL);
    server->priv->number = number;
    g_free (server->priv->address);
    server->priv->address = NULL;
}

guint
xserver_get_display_number (XServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->number;
}

const gchar *
xserver_get_address (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);

    if (!server->priv->address)
    {
        if (server->priv->hostname)
            server->priv->address = g_strdup_printf("%s:%d", server->priv->hostname, server->priv->number);
        else
            server->priv->address = g_strdup_printf(":%d", server->priv->number);
    }  

    return server->priv->address;
}

void
xserver_set_authentication (XServer *server, const gchar *name, const guint8 *data, gsize data_length)
{
    g_return_if_fail (server != NULL);

    g_free (server->priv->authentication_name);
    server->priv->authentication_name = g_strdup (name);
    g_free (server->priv->authentication_data);
    server->priv->authentication_data = g_malloc (data_length);
    server->priv->authentication_data_length = data_length;
    memcpy (server->priv->authentication_data, data, data_length);
}

const gchar *
xserver_get_authentication_name (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->authentication_name;
}

const guint8 *
xserver_get_authentication_data (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->authentication_data;
}

gsize
xserver_get_authentication_data_length (XServer *server)
{
    g_return_val_if_fail (server != NULL, 0);
    return server->priv->authentication_data_length;
}

static void
write_authority_file (XServer *server)
{
    gchar *path;
    GError *error = NULL;

    /* Get file to write to if have authorization */
    if (server->priv->authorization && !server->priv->authority_file)
    {
        gchar *run_dir, *dir;
      
        run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");
        dir = g_build_filename (run_dir, "root", NULL);
        g_free (run_dir);
        g_mkdir_with_parents (dir, S_IRWXU);

        path = g_build_filename (dir, xserver_get_address (server), NULL);
        g_free (dir);
        server->priv->authority_file = g_file_new_for_path (path);
        g_free (path);
    }

    /* Delete existing file if no authorization */
    if (!server->priv->authorization)
    {
        if (server->priv->authority_file)
        {
            path = g_file_get_path (server->priv->authority_file);
            g_debug ("Deleting X server authority %s", path);
            g_free (path);

            g_file_delete (server->priv->authority_file, NULL, NULL);
            g_object_unref (server->priv->authority_file);
            server->priv->authority_file = NULL;
        }
        return;
    }

    path = g_file_get_path (server->priv->authority_file);
    g_debug ("Writing X server authority to %s", path);
    g_free (path);

    if (!xauth_write (server->priv->authorization, XAUTH_WRITE_MODE_SET, NULL, server->priv->authority_file, &error))
        g_warning ("Failed to write authority: %s", error->message);
    g_clear_error (&error);
}

void
xserver_set_authorization (XServer *server, XAuthorization *authorization)
{
    g_return_if_fail (server != NULL);

    if (server->priv->authorization)
        g_object_unref (server->priv->authorization);
    if (authorization)
        server->priv->authorization = g_object_ref (authorization);
    else
        server->priv->authorization = NULL;

    write_authority_file (server);
}

XAuthorization *
xserver_get_authorization (XServer *server)
{
    g_return_val_if_fail (server != NULL, NULL);
    return server->priv->authorization;
}

GFile *
xserver_get_authority_file (XServer *server)
{
    if (!server->priv->authority_file)
        write_authority_file (server);
  
    return server->priv->authority_file;
}

static void
xserver_init (XServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_TYPE, XServerPrivate);
    server->priv->authentication_name = g_strdup ("");
}

static void
xserver_finalize (GObject *object)
{
    XServer *self;
  
    self = XSERVER (object);

    g_free (self->priv->hostname);
    g_free (self->priv->authentication_name);
    g_free (self->priv->authentication_data);
    g_free (self->priv->address);
    if (self->priv->authorization)
        g_object_unref (self->priv->authorization);
    if (self->priv->authority_file)
    {
        g_file_delete (self->priv->authority_file, NULL, NULL);
        g_object_unref (self->priv->authority_file);
    }

    G_OBJECT_CLASS (xserver_parent_class)->finalize (object);
}

static void
xserver_class_init (XServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = xserver_finalize;

    g_type_class_add_private (klass, sizeof (XServerPrivate));
}
