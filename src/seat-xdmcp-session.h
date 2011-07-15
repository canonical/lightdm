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

#ifndef _SEAT_XDMCP_SESSION_H_
#define _SEAT_XDMCP_SESSION_H_

#include "seat.h"
#include "xdmcp-session.h"

G_BEGIN_DECLS

#define SEAT_XDMCP_SESSION_TYPE (seat_xdmcp_session_get_type())
#define SEAT_XDMCP_SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_XDMCP_SESSION_TYPE, SeatXDMCPSession))

typedef struct SeatXDMCPSessionPrivate SeatXDMCPSessionPrivate;

typedef struct
{
    Seat                     parent_instance;
    SeatXDMCPSessionPrivate *priv;
} SeatXDMCPSession;

typedef struct
{
    SeatClass parent_class;
} SeatXDMCPSessionClass;

GType seat_xdmcp_session_get_type (void);

SeatXDMCPSession *seat_xdmcp_session_new (const gchar *config_section, XDMCPSession *session);

G_END_DECLS

#endif /* _SEAT_XDMCP_SESSION_H_ */
