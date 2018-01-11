/*
 * Copyright (C) 2010-2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#ifndef X_SERVER_XMIR_H_
#define X_SERVER_XMIR_H_

#include "x-server-local.h"
#include "unity-system-compositor.h"

G_BEGIN_DECLS

#define X_SERVER_XMIR_TYPE    (x_server_xmir_get_type())
#define X_SERVER_XMIR(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), X_SERVER_XMIR_TYPE, XServerXmir))
#define IS_X_SERVER_XMIR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), X_SERVER_XMIR_TYPE))

typedef struct XServerXmirPrivate XServerXmirPrivate;

typedef struct
{
    XServerLocal        parent_instance;
    XServerXmirPrivate *priv;
} XServerXmir;

typedef struct
{
    XServerLocalClass parent_class;
} XServerXmirClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (XServerXmir, g_object_unref)

GType x_server_xmir_get_type (void);

XServerXmir *x_server_xmir_new (UnitySystemCompositor *compositor);

void x_server_xmir_set_mir_id (XServerXmir *server, const gchar *id);

const gchar *x_server_xmir_get_mir_id (XServerXmir *server);

void x_server_xmir_set_mir_socket (XServerXmir *server, const gchar *socket);

const gchar *x_server_xmir_get_authority_file_path (XServerXmir *server);

G_END_DECLS

#endif /* X_SERVER_XMIR_H_ */
