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

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "display-manager.h"
#include "configuration.h"
#include "display.h"
#include "xdmcp-server.h"
#include "seat-xlocal.h"
#include "seat-xdmcp-session.h"
#include "plymouth.h"

enum {
    STARTED,
    SEAT_ADDED,
    SEAT_REMOVED,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct DisplayManagerPrivate
{
    /* The seats available */
    GList *seats;

    /* XDMCP server */
    XDMCPServer *xdmcp_server;

    /* TRUE if stopping the display manager (waiting for seats to stop) */
    gboolean stopping;
};

G_DEFINE_TYPE (DisplayManager, display_manager, G_TYPE_OBJECT);

DisplayManager *
display_manager_new (void)
{
    return g_object_new (DISPLAY_MANAGER_TYPE, NULL);
}

GList *
display_manager_get_seats (DisplayManager *manager)
{
    return manager->priv->seats;
}

static gboolean
add_seat (DisplayManager *manager, Seat *seat)
{
    gboolean result;
  
    result = seat_start (SEAT (seat));
    if (!result)
        return FALSE;

    manager->priv->seats = g_list_append (manager->priv->seats, g_object_ref (seat));
    g_signal_emit (manager, signals[SEAT_ADDED], 0, seat);

    return TRUE;
}

static gboolean
xdmcp_session_cb (XDMCPServer *server, XDMCPSession *session, DisplayManager *manager)
{
    SeatXDMCPSession *seat;
    gboolean result;

    seat = seat_xdmcp_session_new (session);
    result = add_seat (manager, SEAT (seat));
    g_object_unref (seat);
  
    return result;
}

void
display_manager_start (DisplayManager *manager)
{
    gchar *seats;
    gchar **tokens, **i;

    g_return_if_fail (manager != NULL);

    /* Load the seat modules */
    seat_register_module ("xlocal", SEAT_XLOCAL_TYPE);

    /* Load the static display entries */
    seats = config_get_string (config_get_instance (), "LightDM", "seats");
    if (!seats)
        seats = g_strdup ("");
    tokens = g_strsplit (seats, " ", -1);
    g_free (seats);

    /* Start each static display */
    for (i = tokens; *i; i++)
    {
        gchar *config_section = *i;
        gchar *type;
        Seat *seat;

        g_debug ("Loading seat %s", config_section);

        type = config_get_string (config_get_instance (), config_section, "type");
        if (!type)
            type = config_get_string (config_get_instance (), "SeatDefaults", "type");
        if (!type)
        {
            g_debug ("Seat missing type field");
            continue;
        }

        seat = seat_new (type, config_section);
        if (seat)
        {
            if (!add_seat (manager, seat))
                g_warning ("Failed to start seat %s", config_section);
            g_object_unref (seat);
        }
        else
            g_debug ("Unknown seat type %s", type);
    }
    g_strfreev (tokens);

    /* Disable Plymouth if no X servers are replacing it */
    if (plymouth_get_is_active ())
    {
        g_debug ("Stopping Plymouth, no displays replace it");      
        plymouth_quit (FALSE);
    }

    if (config_get_boolean (config_get_instance (), "XDMCPServer", "enabled"))
    {
        gchar *key;

        manager->priv->xdmcp_server = xdmcp_server_new ();
        if (config_has_key (config_get_instance (), "XDMCPServer", "port"))
        {
            gint port;
            port = config_get_integer (config_get_instance (), "XDMCPServer", "port");
            if (port > 0)
                xdmcp_server_set_port (manager->priv->xdmcp_server, port);
        }
        g_signal_connect (manager->priv->xdmcp_server, "new-session", G_CALLBACK (xdmcp_session_cb), manager);

        key = config_get_string (config_get_instance (), "XDMCPServer", "key");
        if (key)
            xdmcp_server_set_key (manager->priv->xdmcp_server, key);
        g_free (key);

        g_debug ("Starting XDMCP server on UDP/IP port %d", xdmcp_server_get_port (manager->priv->xdmcp_server));
        xdmcp_server_start (manager->priv->xdmcp_server); 
    }

    g_signal_emit (manager, signals[STARTED], 0);
}

static gboolean
check_stopped (DisplayManager *manager)
{
    if (g_list_length (manager->priv->seats) == 0)
    {
        g_debug ("Display manager stopped");
        g_signal_emit (manager, signals[STOPPED], 0);
        return TRUE;
    }
    return FALSE;
}

static void
seat_stopped_cb (Seat *seat, DisplayManager *manager)
{
    manager->priv->seats = g_list_remove (manager->priv->seats, seat);
    g_signal_handlers_disconnect_matched (seat, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, manager);

    if (manager->priv->stopping)
    {      
        check_stopped (manager);
        return;
    }

    g_signal_emit (manager, signals[SEAT_REMOVED], 0, seat);
}

void
display_manager_stop (DisplayManager *manager)
{
    GList *link;

    g_return_if_fail (manager != NULL);

    if (manager->priv->stopping)
        return;

    g_debug ("Stopping display manager");

    manager->priv->stopping = TRUE;

    if (manager->priv->xdmcp_server)
    {
        // FIXME: xdmcp_server_stop
        g_object_unref (manager->priv->xdmcp_server);
        manager->priv->xdmcp_server = NULL;
    }

    if (check_stopped (manager))
        return;

    for (link = manager->priv->seats; link; link = link->next)
    {
        Seat *seat = link->data;
        g_signal_connect (seat, "stopped", G_CALLBACK (seat_stopped_cb), manager);
        seat_stop (seat);
    }
}
  
static void
display_manager_init (DisplayManager *manager)
{
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, DISPLAY_MANAGER_TYPE, DisplayManagerPrivate);
}

static void
display_manager_finalize (GObject *object)
{
    DisplayManager *self;
    GList *link;

    self = DISPLAY_MANAGER (object);

    if (self->priv->xdmcp_server)
        g_object_unref (self->priv->xdmcp_server);
    for (link = self->priv->seats; link; link = link->next)
        g_object_unref (link->data);
    g_list_free (self->priv->seats);

    G_OBJECT_CLASS (display_manager_parent_class)->finalize (object);
}

static void
display_manager_class_init (DisplayManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = display_manager_finalize;

    g_type_class_add_private (klass, sizeof (DisplayManagerPrivate));

    signals[STARTED] =
        g_signal_new ("started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, started),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
    signals[SEAT_ADDED] =
        g_signal_new ("seat-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, seat_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, SEAT_TYPE);
    signals[SEAT_REMOVED] =
        g_signal_new ("seat-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, seat_removed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, SEAT_TYPE);
    signals[STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, stopped),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
