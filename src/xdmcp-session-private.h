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

#ifndef _XDMCP_SESSION_PRIVATE_H_
#define _XDMCP_SESSION_PRIVATE_H_

struct XDMCPSessionPrivate
{
    guint16 id;

    gchar *manufacturer_display_id;

    GInetAddress *address;

    GInetAddress *address6;

    gchar *authorization_name;
  
    gboolean started;

    guint16 display_number;

    gchar *display_class;
};

void xdmcp_session_manage (XDMCPSession *session, guint16 display_number, const gchar *display_class);

#endif /* _XDMCP_SESSION_PRIVATE_H_ */
