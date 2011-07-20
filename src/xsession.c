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

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "xsession.h"
#include "configuration.h"

struct XSessionPrivate
{
    /* X server connected to */
    XServer *xserver;

    /* X Authority */
    XAuthority *authority;
    GFile *authority_file;
};

G_DEFINE_TYPE (XSession, xsession, SESSION_TYPE);

XSession *
xsession_new (XServer *xserver)
{
    XSession *session = g_object_new (XSESSION_TYPE, NULL);
  
    session->priv->xserver = g_object_ref (xserver);

    return session;
}

static gboolean
xsession_start (Session *session)
{
    if (xserver_get_authority (XSESSION (session)->priv->xserver))
    {
        gchar *path;
        GError *error = NULL;

        XSESSION (session)->priv->authority = g_object_ref (xserver_get_authority (XSESSION (session)->priv->xserver));
      
        if (config_get_boolean (config_get_instance (), "LightDM", "user-authority-in-system-dir"))
        {
            gchar *run_dir, *dir;

            run_dir = config_get_string (config_get_instance (), "LightDM", "run-directory");          
            dir = g_build_filename (run_dir, user_get_name (session_get_user (session)), NULL);
            g_free (run_dir);

            g_mkdir_with_parents (dir, S_IRWXU);
            if (getuid () == 0)
            {
                if (chown (dir, user_get_uid (session_get_user (session)), user_get_gid (session_get_user (session))) < 0)
                    g_warning ("Failed to set ownership of user authority dir: %s", strerror (errno));
            }

            path = g_build_filename (dir, "xauthority", NULL);
            g_free (dir);

            process_set_env (PROCESS (session), "XAUTHORITY", path);
        }
        else
            path = g_build_filename (user_get_home_directory (session_get_user (session)), ".Xauthority", NULL);

        XSESSION (session)->priv->authority_file = g_file_new_for_path (path);
        g_free (path);

        g_debug ("Adding session authority to %s", g_file_get_path (XSESSION (session)->priv->authority_file));
        if (!xauth_write (XSESSION (session)->priv->authority, XAUTH_WRITE_MODE_REPLACE, session_get_user (session), XSESSION (session)->priv->authority_file, &error))
            g_warning ("Failed to write authority: %s", error->message);
        g_clear_error (&error);
    }

    process_set_env (PROCESS (session), "DISPLAY", xserver_get_address (XSESSION (session)->priv->xserver));

    return SESSION_CLASS (xsession_parent_class)->start (session);
}

static void
xsession_remove_authority (XSession *session)
{
    if (session->priv->authority_file)
    {
        g_debug ("Removing session authority from %s", g_file_get_path (session->priv->authority_file));
        xauth_write (session->priv->authority, XAUTH_WRITE_MODE_REMOVE, session_get_user (SESSION (session)), session->priv->authority_file, NULL);
        g_object_unref (session->priv->authority_file);
        session->priv->authority_file = NULL;
    }
    if (session->priv->authority)
    {
        g_object_unref (session->priv->authority);
        session->priv->authority = NULL;
    }
}

static void
xsession_stop (Session *session)
{
    xsession_remove_authority (XSESSION (session));
    SESSION_CLASS (xsession_parent_class)->stop (session);
}

static void
xsession_init (XSession *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, XSESSION_TYPE, XSessionPrivate);
}

static void
xsession_finalize (GObject *object)
{
    XSession *self;

    self = XSESSION (object);

    xsession_remove_authority (self);
    if (self->priv->xserver)
        g_object_unref (self->priv->xserver);

    G_OBJECT_CLASS (xsession_parent_class)->finalize (object);
}

static void
xsession_class_init (XSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    SessionClass *session_class = SESSION_CLASS (klass);

    session_class->start = xsession_start;
    session_class->stop = xsession_stop;
    object_class->finalize = xsession_finalize;

    g_type_class_add_private (klass, sizeof (XSessionPrivate));
}
