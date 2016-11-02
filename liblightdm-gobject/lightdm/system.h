/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef LIGHTDM_HOSTNAME_H_
#define LIGHTDM_HOSTNAME_H_

#include <glib-object.h>

G_BEGIN_DECLS

const gchar *lightdm_get_hostname (void);

const gchar *lightdm_get_os_id (void);

const gchar *lightdm_get_os_name (void);

const gchar *lightdm_get_os_pretty_name (void);

const gchar *lightdm_get_os_version (void);

const gchar *lightdm_get_os_version_id (void);

gchar *lightdm_get_motd (void);

G_END_DECLS

#endif /* LIGHTDM_HOSTNAME_H_ */
