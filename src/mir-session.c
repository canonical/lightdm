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

#include "mir-session.h"

struct MirSessionPrivate
{
    /* Mir server information */
    MirServer *mir_server;
};

G_DEFINE_TYPE (MirSession, mir_session, SESSION_TYPE);

MirSession *
mir_session_new (MirServer *mir_server)
{
    MirSession *session;

    session = g_object_new (MIR_SESSION_TYPE, NULL);
    session->priv->mir_server = g_object_ref (mir_server);

    return session;
}

static void
mir_session_init (MirSession *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, MIR_SESSION_TYPE, MirSessionPrivate);
}

static void
mir_session_finalize (GObject *object)
{
    MirSession *self;

    self = MIR_SESSION (object);

    if (self->priv->mir_server)
        g_object_unref (self->priv->mir_server);

    G_OBJECT_CLASS (mir_session_parent_class)->finalize (object);
}

static void
mir_session_class_init (MirSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = mir_session_finalize;

    g_type_class_add_private (klass, sizeof (MirSessionPrivate));
}
