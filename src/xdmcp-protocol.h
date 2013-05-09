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

#ifndef XDMCP_PROTOCOL_H_
#define XDMCP_PROTOCOL_H_

#include <glib.h>

#define XDMCP_VERSION 1

typedef enum
{
    XDMCP_BroadcastQuery = 1,
    XDMCP_Query          = 2,
    XDMCP_IndirectQuery  = 3,
    XDMCP_ForwardQuery   = 4,
    XDMCP_Willing        = 5,
    XDMCP_Unwilling      = 6,
    XDMCP_Request        = 7,
    XDMCP_Accept         = 8,
    XDMCP_Decline        = 9,
    XDMCP_Manage         = 10,
    XDMCP_Refuse         = 11,
    XDMCP_Failed         = 12,
    XDMCP_KeepAlive      = 13,
    XDMCP_Alive          = 14
} XDMCPOpcode;

typedef struct
{
    guint16 length;
    guchar *data;
} XDMCPData;

typedef struct
{
    guint16 type;
    XDMCPData address;
} XDMCPConnection;

typedef struct
{
    XDMCPOpcode opcode;
  
    union
    {
        struct
        {
            gchar **authentication_names;
        } Query;

        struct
        {
            gchar *client_address;
            gchar *client_port;
            gchar **authentication_names;
        } ForwardQuery;

        struct
        {
            gchar *authentication_name;
            gchar *hostname;
            gchar *status;
        } Willing;

        struct
        {
            gchar *hostname;
            gchar *status;
        } Unwilling;

        struct
        {
            guint16 display_number;
            guint8 n_connections;
            XDMCPConnection *connections;
            gchar *authentication_name;
            XDMCPData authentication_data;
            gchar **authorization_names;
            gchar *manufacturer_display_id;
        } Request;

        struct
        {
            guint32 session_id;
            gchar *authentication_name;
            XDMCPData authentication_data;
            gchar *authorization_name;
            XDMCPData authorization_data;
        } Accept;

        struct
        {
            gchar *status;
            gchar *authentication_name;
            XDMCPData authentication_data;
        } Decline;

        struct
        {
            guint32 session_id;
            guint16 display_number;
            gchar *display_class;
        } Manage;

        struct
        {
            guint32 session_id;
        } Refuse;

        struct
        {
            guint32 session_id;
            gchar *status;
        } Failed;

        struct
        {
            guint16 display_number;
            guint32 session_id;
        } KeepAlive;

        struct
        {
            gboolean session_running;
            guint32 session_id;
        } Alive;
    };
} XDMCPPacket;

XDMCPPacket *xdmcp_packet_alloc (XDMCPOpcode opcode);

XDMCPPacket *xdmcp_packet_decode (const guchar *data, gsize length);

gssize xdmcp_packet_encode (XDMCPPacket *packet, guchar *data, gsize length);

gchar *xdmcp_packet_tostring (XDMCPPacket *packet);

void xdmcp_packet_free (XDMCPPacket *packet);

#endif /* XDMCP_PROTOCOL_H_ */
