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

#include "xserver.h"
#include "session.h"
#include "greeter.h"

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
  
    void (*started)(Display *display);
    void (*start_greeter)(Display *display, Session *session);
    void (*end_greeter)(Display *display, Session *session);
    gboolean (*activate_user)(Display *display, const gchar *username);
    void (*start_session)(Display *display, Session *session);
    void (*end_session)(Display *display, Session *session);
    void (*stopped)(Display *display);
} DisplayClass;

GType display_get_type (void);

Display *display_new (XServer *xserver);

XServer *display_get_xserver (Display *display);

Greeter *display_get_greeter (Display *display);

void display_set_session_wrapper (Display *display, const gchar *session_wrapper);

const gchar *display_get_session_wrapper (Display *display);

void display_set_default_user (Display *display, const gchar *username);

const gchar *display_get_default_user (Display *display);

void display_set_default_user_timeout (Display *display, gint timeout);

gint display_get_default_user_timeout (Display *display);

void display_set_greeter_user (Display *display, const gchar *username);

const gchar *display_get_greeter_user (Display *display);

const gchar *display_get_session_user (Display *display);

void display_set_greeter_theme (Display *display, const gchar *greeter_theme);

const gchar *display_get_greeter_theme (Display *display);

void display_set_default_session (Display *display, const gchar *session);

const gchar *display_get_default_session (Display *display);

void display_set_pam_service (Display *display, const gchar *service);

const gchar *display_get_pam_service (Display *display);

void display_set_pam_autologin_service (Display *display, const gchar *service);

const gchar *display_get_pam_autologin_service (Display *display);

void display_set_vt (Display *display, gint vt);

gint display_get_vt (Display *display);

gboolean display_start (Display *display);

void display_show (Display *display);

void display_stop (Display *display);

G_END_DECLS

#endif /* _DISPLAY_H_ */
