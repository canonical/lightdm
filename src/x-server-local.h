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

#ifndef X_SERVER_LOCAL_H_
#define X_SERVER_LOCAL_H_

#include "x-server.h"
#include "process.h"

G_BEGIN_DECLS

#define X_SERVER_LOCAL_TYPE    (x_server_local_get_type())
#define X_SERVER_LOCAL(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), X_SERVER_LOCAL_TYPE, XServerLocal))
#define X_SERVER_LOCAL_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), X_SERVER_LOCAL_TYPE, XServerLocalClass))
#define X_SERVER_LOCAL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), X_SERVER_LOCAL_TYPE, XServerLocalClass))
#define IS_X_SERVER_LOCAL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), X_SERVER_LOCAL_TYPE))

typedef struct XServerLocalPrivate XServerLocalPrivate;

typedef struct
{
    XServer              parent_instance;
    XServerLocalPrivate *priv;
} XServerLocal;

typedef struct
{
    XServerClass parent_class;
    ProcessRunFunc (*get_run_function)(XServerLocal *server);
    gboolean (*get_log_stdout)(XServerLocal *server);  
    void (*add_args)(XServerLocal *server, GString *command);
} XServerLocalClass;

const gchar *x_server_local_get_version (void);

gint x_server_local_version_compare (guint major, guint minor);

guint x_server_local_get_unused_display_number (void);

void x_server_local_release_display_number (guint display_number);

GType x_server_local_get_type (void);

XServerLocal *x_server_local_new (void);

void x_server_local_set_command (XServerLocal *server, const gchar *command);

void x_server_local_set_vt (XServerLocal *server, gint vt);

void x_server_local_set_config (XServerLocal *server, const gchar *path);

void x_server_local_set_layout (XServerLocal *server, const gchar *layout);

void x_server_local_set_xdg_seat (XServerLocal *server, const gchar *xdg_seat);

void x_server_local_set_allow_tcp (XServerLocal *server, gboolean allow_tcp);

void x_server_local_set_xdmcp_server (XServerLocal *server, const gchar *hostname);

const gchar *x_server_local_get_xdmcp_server (XServerLocal *server);

void x_server_local_set_xdmcp_port (XServerLocal *server, guint port);

guint x_server_local_get_xdmcp_port (XServerLocal *server);

void x_server_local_set_xdmcp_key (XServerLocal *server, const gchar *key);

void x_server_local_set_background (XServerLocal *server, const gchar *background);

void x_server_local_set_mir_id (XServerLocal *server, const gchar *id);

const gchar *x_server_local_get_mir_id (XServerLocal *server);

void x_server_local_set_mir_socket (XServerLocal *server, const gchar *socket);

const gchar *x_server_local_get_authority_file_path (XServerLocal *server);

G_END_DECLS

#endif /* X_SERVER_LOCAL_H_ */
