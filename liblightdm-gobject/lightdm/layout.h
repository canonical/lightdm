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

#ifndef _LDM_LAYOUT_H_
#define _LDM_LAYOUT_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LDM_TYPE_LAYOUT            (ldm_layout_get_type())
#define LDM_LAYOUT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LDM_TYPE_LAYOUT, LdmLayout));
#define LDM_LAYOUT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LDM_TYPE_LAYOUT, LdmLayoutClass))
#define LDM_IS_LAYOUT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LDM_TYPE_LAYOUT))
#define LDM_IS_LAYOUT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LDM_TYPE_LAYOUT))
#define LDM_LAYOUT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LDM_TYPE_LAYOUT, LdmLayoutClass))

typedef struct _LdmLayout        LdmLayout;
typedef struct _LdmLayoutClass   LdmLayoutClass;
typedef struct _LdmLayoutPrivate LdmLayoutPrivate;

struct _LdmLayout
{
    GObject         parent_instance;
    /*<private>*/
    LdmLayoutPrivate *priv;
};

struct _LdmLayoutClass
{
    GObjectClass parent_class;
};

GType ldm_layout_get_type (void);

LdmLayout *ldm_layout_new (const gchar *name, const gchar *short_description, const gchar *description);

const gchar *ldm_layout_get_name (LdmLayout *layout);

const gchar *ldm_layout_get_short_description (LdmLayout *layout);

const gchar *ldm_layout_get_description (LdmLayout *layout);

G_END_DECLS

#endif /* _LDM_LAYOUT_H_ */
