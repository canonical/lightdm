/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef _LIGHTDM_LANGUAGE_H_
#define _LIGHTDM_LANGUAGE_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LIGHTDM_TYPE_LANGUAGE            (lightdm_language_get_type())
#define LIGHTDM_LANGUAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIGHTDM_TYPE_LANGUAGE, LightDMLanguage));
#define LIGHTDM_LANGUAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LIGHTDM_TYPE_LANGUAGE, LightDMLanguageClass))
#define LIGHTDM_IS_LANGUAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIGHTDM_TYPE_LANGUAGE))
#define LIGHTDM_IS_LANGUAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LIGHTDM_TYPE_LANGUAGE))
#define LIGHTDM_LANGUAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LIGHTDM_TYPE_LANGUAGE, LightDMLanguageClass))

typedef struct
{
    GObject parent_instance;
} LightDMLanguage;

typedef struct
{
    GObjectClass parent_class;

	/* Reserved */
	void (*reserved1) (void);
	void (*reserved2) (void);
	void (*reserved3) (void);
	void (*reserved4) (void);
	void (*reserved5) (void);
	void (*reserved6) (void);
} LightDMLanguageClass;

GType lightdm_language_get_type (void);

GList *lightdm_get_languages (void);

LightDMLanguage *lightdm_get_language (void);

const gchar *lightdm_language_get_code (LightDMLanguage *language);

const gchar *lightdm_language_get_name (LightDMLanguage *language);

const gchar *lightdm_language_get_territory (LightDMLanguage *language);

gboolean lightdm_language_matches (LightDMLanguage *language, const gchar *code);

G_END_DECLS

#endif /* _LIGHTDM_LANGUAGE_H_ */
