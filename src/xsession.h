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

#ifndef _XSESSION_H_
#define _XSESSION_H_

#include "session.h"
#include "xserver.h"

G_BEGIN_DECLS

#define XSESSION_TYPE (xsession_get_type())
#define XSESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), XSESSION_TYPE, XSession))

typedef struct XSessionPrivate XSessionPrivate;

typedef struct
{
    Session          parent_instance;
    XSessionPrivate *priv;
} XSession;

typedef struct
{
    SessionClass parent_class;
} XSessionClass;

GType xsession_get_type (void);

XSession *xsession_new (XServer *xserver);

G_END_DECLS

#endif /* _XSESSION_H_ */
