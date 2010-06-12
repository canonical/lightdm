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

#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <glib-object.h>
#include <dbus/dbus-glib.h>

#include "session-manager.h"

G_BEGIN_DECLS

#define DISPLAY_TYPE (display_get_type())
#define DISPLAY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), DISPLAY_TYPE, Display));

typedef struct DisplayPrivate DisplayPrivate;

typedef struct
{
    GObject         parent_instance;
    DisplayPrivate *priv;
} Display;

typedef struct
{
    GObjectClass parent_class;
  
    void (*exited)(Display *display);
} DisplayClass;

GType display_get_type (void);

Display *display_new (GKeyFile *config, SessionManager *sessions, gint index);

gint display_get_index (Display *display);

void display_start (Display *display, const gchar *session, const gchar *username, gint timeout);

gboolean display_connect (Display *display, const gchar **session, const gchar **username, gint *delay, GError *error);

gboolean display_set_session (Display *display, const gchar *session, GError *error);

gboolean display_start_authentication (Display *display, const gchar *username, DBusGMethodInvocation *context);

gboolean display_continue_authentication (Display *display, gchar **secrets, DBusGMethodInvocation *context);

G_END_DECLS

#endif /* _DISPLAY_H_ */
