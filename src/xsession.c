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
#include "privileges.h"

struct XSessionPrivate
{
    /* X server connected to */
    XServer *xserver;
};

G_DEFINE_TYPE (XSession, xsession, SESSION_TYPE);

XSession *
xsession_new (XServer *xserver)
{
    XSession *session;
    XAuthority *authority;

    session = g_object_new (XSESSION_TYPE, NULL);
    session->priv->xserver = g_object_ref (xserver);

    session_set_env (SESSION (session), "DISPLAY", xserver_get_address (xserver));
    session_set_tty (SESSION (session), xserver_get_address (xserver));
    session_set_xdisplay (SESSION (session), xserver_get_address (xserver));
    authority = xserver_get_authority (xserver);
    if (authority)
        session_set_xauthority (SESSION (session), authority, config_get_boolean (config_get_instance (), "LightDM", "user-authority-in-system-dir"));
    session_set_log_file (SESSION (session), ".xsession-errors", LOG_MODE_BACKUP_AND_TRUNCATE);

    return session;
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

    if (self->priv->xserver)
        g_object_unref (self->priv->xserver);

    G_OBJECT_CLASS (xsession_parent_class)->finalize (object);
}

static void
xsession_class_init (XSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = xsession_finalize;

    g_type_class_add_private (klass, sizeof (XSessionPrivate));
}
