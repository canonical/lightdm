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

#ifndef _SEAT_XDMCP_CLIENT_H_
#define _SEAT_XDMCP_CLIENT_H_

#include "seat.h"

G_BEGIN_DECLS

#define SEAT_XDMCP_CLIENT_TYPE (seat_xdmcp_client_get_type())
#define SEAT_XDMCP_CLIENT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SEAT_XDMCP_CLIENT_TYPE, SeatXDMCPClient))

typedef struct SeatXDMCPClientPrivate SeatXDMCPClientPrivate;

typedef struct
{
    Seat                    parent_instance;
    SeatXDMCPClientPrivate *priv;
} SeatXDMCPClient;

typedef struct
{
    SeatClass parent_class;
} SeatXDMCPClientClass;

GType seat_xdmcp_client_get_type (void);

SeatXDMCPClient *seat_xdmcp_client_new (const gchar *config_section);

G_END_DECLS

#endif /* _SEAT_XDMCP_CLIENT_H_ */
