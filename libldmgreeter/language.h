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

#ifndef _LDM_LANGUAGE_H_
#define _LDM_LANGUAGE_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LDM_TYPE_LANGUAGE            (ldm_language_get_type())
#define LDM_LANGUAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LDM_TYPE_LANGUAGE, LdmLanguage));
#define LDM_LANGUAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LDM_TYPE_LANGUAGE, LdmLanguageClass))
#define LDM_IS_LANGUAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LDM_TYPE_LANGUAGE))
#define LDM_IS_LANGUAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LDM_TYPE_LANGUAGE))
#define LDM_LANGUAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LDM_TYPE_LANGUAGE, LdmLanguageClass))

typedef struct _LdmLanguage        LdmLanguage;
typedef struct _LdmLanguageClass   LdmLanguageClass;
typedef struct _LdmLanguagePrivate LdmLanguagePrivate;

struct _LdmLanguage
{
    GObject         parent_instance;
    LdmLanguagePrivate *priv;
};

struct _LdmLanguageClass
{
    GObjectClass parent_class;
};

GType ldm_language_get_type (void);

LdmLanguage *ldm_language_new (const gchar *code);

const gchar *ldm_language_get_code (LdmLanguage *language);

const gchar *ldm_language_get_name (LdmLanguage *language);

const gchar *ldm_language_get_territory (LdmLanguage *language);

G_END_DECLS

#endif /* _LDM_LANGUAGE_H_ */
