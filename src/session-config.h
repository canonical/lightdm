/*
 * Copyright (C) 2013 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef SESSION_CONFIG_H_
#define SESSION_CONFIG_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define SESSION_CONFIG_TYPE           (session_config_get_type())
#define SESSION_CONFIG(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), SESSION_CONFIG_TYPE, SessionConfig))
#define SESSION_CONFIG_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), SESSION_CONFIG_TYPE, SessionConfigClass))
#define SESSION_CONFIG_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SESSION_CONFIG_TYPE, SessionConfigClass))

typedef struct SessionConfigPrivate SessionConfigPrivate;

typedef struct
{
    GObject               parent_instance;
    SessionConfigPrivate *priv;
} SessionConfig;

typedef struct
{
    GObjectClass parent_class;
} SessionConfigClass;

GType session_config_get_type (void);

SessionConfig *session_config_new_from_file (const gchar *filename, GError **error);

const gchar *session_config_get_command (SessionConfig *config);

const gchar *session_config_get_session_type (SessionConfig *config);

const gchar *session_config_get_desktop_name (SessionConfig *config);

G_END_DECLS

#endif /* SESSION_CONFIG_H_ */
