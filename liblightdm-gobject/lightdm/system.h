/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#ifndef _LIGHTDM_HOSTNAME_H_
#define _LIGHTDM_HOSTNAME_H_

#include <glib-object.h>

G_BEGIN_DECLS

const gchar *lightdm_get_hostname (void);

G_END_DECLS

#endif /* _LIGHTDM_HOSTNAME_H_ */
