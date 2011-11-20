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

#ifndef _LIGHTDM_POWER_H_
#define _LIGHTDM_POWER_H_

G_BEGIN_DECLS

gboolean lightdm_get_can_suspend (void);

gboolean lightdm_suspend (GError **error);

gboolean lightdm_get_can_hibernate (void);

gboolean lightdm_hibernate (GError **error);

gboolean lightdm_get_can_restart (void);

gboolean lightdm_restart (GError **error);

gboolean lightdm_get_can_shutdown (void);

gboolean lightdm_shutdown (GError **error);

G_END_DECLS

#endif /* _LIGHTDM_POWER_H_ */
