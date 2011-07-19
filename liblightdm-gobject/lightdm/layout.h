/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#ifndef _LIGHTDM_LAYOUT_H_
#define _LIGHTDM_LAYOUT_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LIGHTDM_TYPE_LAYOUT            (lightdm_layout_get_type())
#define LIGHTDM_LAYOUT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIGHTDM_TYPE_LAYOUT, LightDMLayout));
#define LIGHTDM_LAYOUT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LIGHTDM_TYPE_LAYOUT, LightDMLayoutClass))
#define LIGHTDM_IS_LAYOUT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIGHTDM_TYPE_LAYOUT))
#define LIGHTDM_IS_LAYOUT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LIGHTDM_TYPE_LAYOUT))
#define LIGHTDM_LAYOUT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LIGHTDM_TYPE_LAYOUT, LightDMLayoutClass))

typedef struct
{
    GObject parent_instance;
} LightDMLayout;

typedef struct
{
    GObjectClass parent_class;
} LightDMLayoutClass;

GType lightdm_layout_get_type (void);

LightDMLayout *lightdm_layout_new (const gchar *name, const gchar *short_description, const gchar *description);

const gchar *lightdm_layout_get_name (LightDMLayout *layout);

const gchar *lightdm_layout_get_short_description (LightDMLayout *layout);

const gchar *lightdm_layout_get_description (LightDMLayout *layout);

G_END_DECLS

#endif /* _LIGHTDM_LAYOUT_H_ */
