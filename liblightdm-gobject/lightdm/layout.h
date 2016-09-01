/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef LIGHTDM_LAYOUT_H_
#define LIGHTDM_LAYOUT_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LIGHTDM_TYPE_LAYOUT (lightdm_layout_get_type())

G_DECLARE_FINAL_TYPE (LightDMLayout, lightdm_layout, LIGHTDM, LAYOUT, GObject)

struct _LightDMLayoutClass
{
    /*< private >*/
    GObjectClass parent_class;
};

GList *lightdm_get_layouts (void);

void lightdm_set_layout (LightDMLayout *layout);

LightDMLayout *lightdm_get_layout (void);

const gchar *lightdm_layout_get_name (LightDMLayout *layout);

const gchar *lightdm_layout_get_short_description (LightDMLayout *layout);

const gchar *lightdm_layout_get_description (LightDMLayout *layout);

G_END_DECLS

#endif /* LIGHTDM_LAYOUT_H_ */
