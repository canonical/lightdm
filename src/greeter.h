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

#ifndef _GREETER_H_
#define _GREETER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define GREETER_TYPE (greeter_get_type())
#define GREETER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GREETER_TYPE, Greeter));

typedef struct GreeterPrivate GreeterPrivate;

typedef struct
{
    GObject         parent_instance;
    GreeterPrivate *priv;
} Greeter;

typedef struct
{
    GObjectClass parent_class;
  
    void (*show_prompt)(Greeter *greeter, const gchar *text);
    void (*show_message)(Greeter *greeter, const gchar *text);
    void (*show_error)(Greeter *greeter, const gchar *text);
    void (*authentication_complete)(Greeter *greeter);
    void (*timed_login)(Greeter *greeter, const gchar *username);
} GreeterClass;

typedef struct
{
   const char *name;
   const char *real_name;
} UserInfo;

typedef struct
{
   const char *name;
   const char *comment;
} Session;

GType greeter_get_type (void);

Greeter *greeter_new (void);

gboolean greeter_connect (Greeter *greeter);

gint greeter_get_num_users (Greeter *greeter);

const GList *greeter_get_users (Greeter *greeter);

const GList *greeter_get_sessions (Greeter *greeter);

gchar *greeter_get_timed_login_user (Greeter *greeter);

gint greeter_get_timed_login_delay (Greeter *greeter);

void greeter_cancel_timed_login (Greeter *greeter);

void greeter_do_timed_login (Greeter *greeter);

void greeter_start_authentication (Greeter *greeter, const char *username);

void greeter_provide_secret (Greeter *greeter, const gchar *secret);

void greeter_cancel_authentication (Greeter *greeter);

gboolean greeter_get_is_authenticated (Greeter *greeter);

G_END_DECLS

#endif /* _GREETER_H_ */
