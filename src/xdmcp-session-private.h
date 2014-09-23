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

#ifndef XDMCP_SESSION_PRIVATE_H_
#define XDMCP_SESSION_PRIVATE_H_

#include "xdmcp-server.h"
#include "x-authority.h"

struct XDMCPSessionPrivate
{
    XDMCPServer *server;

    guint16 id;

    gchar *manufacturer_display_id;

    GInetAddress *address;

    guint inactive_timeout;

    XAuthority *authority;

    gboolean started;

    guint16 display_number;

    gchar *display_class;
};

#endif /* XDMCP_SESSION_PRIVATE_H_ */
