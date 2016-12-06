/*
 * Copyright (C) 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef DISPLAY_MANAGER_SERVICE_H_
#define DISPLAY_MANAGER_SERVICE_H_

#include <glib-object.h>

#include "display-manager.h"

G_BEGIN_DECLS

#define DISPLAY_MANAGER_SERVICE_TYPE (display_manager_service_get_type())
#define DISPLAY_MANAGER_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), DISPLAY_MANAGER_SERVICE_TYPE, DisplayManagerService));

#define DISPLAY_MANAGER_SERVICE_SIGNAL_READY           "ready"
#define DISPLAY_MANAGER_SERVICE_SIGNAL_ADD_XLOCAL_SEAT "add-xlocal-seat"
#define DISPLAY_MANAGER_SERVICE_SIGNAL_NAME_LOST       "name-lost"

typedef struct DisplayManagerServicePrivate DisplayManagerServicePrivate;

typedef struct
{
    GObject                       parent_instance;
    DisplayManagerServicePrivate *priv;
} DisplayManagerService;

typedef struct
{
    GObjectClass parent_class;

    void  (*ready)(DisplayManagerService *service);
    Seat *(*add_xlocal_seat)(DisplayManagerService *service, gint display_number);
    void  (*name_lost)(DisplayManagerService *service);
} DisplayManagerServiceClass;

GType display_manager_service_get_type (void);

DisplayManagerService *display_manager_service_new (DisplayManager *manager);

void display_manager_service_start (DisplayManagerService *service);

G_END_DECLS

#endif /* DISPLAY_MANAGER_SERVICE_H_ */
