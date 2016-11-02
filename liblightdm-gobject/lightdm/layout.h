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

#define LIGHTDM_TYPE_LAYOUT            (lightdm_layout_get_type())
#define LIGHTDM_LAYOUT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIGHTDM_TYPE_LAYOUT, LightDMLayout));
#define LIGHTDM_LAYOUT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LIGHTDM_TYPE_LAYOUT, LightDMLayoutClass))
#define LIGHTDM_IS_LAYOUT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIGHTDM_TYPE_LAYOUT))
#define LIGHTDM_IS_LAYOUT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LIGHTDM_TYPE_LAYOUT))
#define LIGHTDM_LAYOUT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LIGHTDM_TYPE_LAYOUT, LightDMLayoutClass))

typedef struct _LightDMLayout          LightDMLayout;
typedef struct _LightDMLayoutClass     LightDMLayoutClass;

struct _LightDMLayout
{
    GObject parent_instance;
};

struct _LightDMLayoutClass
{
    /*< private >*/
    GObjectClass parent_class;

    /* Reserved */
    void (*reserved1) (void);
    void (*reserved2) (void);
    void (*reserved3) (void);
    void (*reserved4) (void);
    void (*reserved5) (void);
    void (*reserved6) (void);
};

#ifdef GLIB_VERSION_2_44
typedef LightDMLayout *LightDMLayout_autoptr;
static inline void glib_autoptr_cleanup_LightDMLayout (LightDMLayout **_ptr)
{
    glib_autoptr_cleanup_GObject ((GObject **) _ptr);
}
#endif

GType lightdm_layout_get_type (void);

GList *lightdm_get_layouts (void);

void lightdm_set_layout (LightDMLayout *layout);

LightDMLayout *lightdm_get_layout (void);

const gchar *lightdm_layout_get_name (LightDMLayout *layout);

const gchar *lightdm_layout_get_short_description (LightDMLayout *layout);

const gchar *lightdm_layout_get_description (LightDMLayout *layout);

G_END_DECLS

#endif /* LIGHTDM_LAYOUT_H_ */
