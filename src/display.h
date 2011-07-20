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

#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <glib-object.h>

#include "display-server.h"
#include "session.h"

G_BEGIN_DECLS

#define DISPLAY_TYPE           (display_get_type())
#define DISPLAY(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), DISPLAY_TYPE, Display))
#define DISPLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), DISPLAY_TYPE, DisplayClass))
#define DISPLAY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), DISPLAY_TYPE, DisplayClass))

typedef struct DisplayPrivate DisplayPrivate;

typedef struct
{
    GObject         parent_instance;
    DisplayPrivate *priv;
} Display;

typedef struct
{
    GObjectClass parent_class;

    Session *(*create_session) (Display *display);

    void (*started)(Display *display);
    gboolean (*activate_user)(Display *display, const gchar *username);
    void (*session_started)(Display *display);
    void (*session_stopped)(Display *display);
    void (*stopped)(Display *display);
} DisplayClass;

GType display_get_type (void);

void display_load_config (Display *display, const gchar *config_section);

void display_set_display_server (Display *display, DisplayServer *display_server);

DisplayServer *display_get_display_server (Display *display);

Session *display_get_session (Display *display);

void display_set_default_user (Display *display, const gchar *username, gboolean is_guest, gboolean requires_password, gint timeout);

gboolean display_start (Display *display);

void display_stop (Display *display);

G_END_DECLS

#endif /* _DISPLAY_H_ */
