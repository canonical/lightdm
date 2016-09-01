/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef LIGHTDM_LANGUAGE_H_
#define LIGHTDM_LANGUAGE_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LIGHTDM_TYPE_LANGUAGE (lightdm_language_get_type())

G_DECLARE_FINAL_TYPE (LightDMLanguage, lightdm_language, LIGHTDM, LANGUAGE, GObject)

struct _LightDMLanguageClass
{
    /*< private >*/
    GObjectClass parent_class;
};

GList *lightdm_get_languages (void);

LightDMLanguage *lightdm_get_language (void);

const gchar *lightdm_language_get_code (LightDMLanguage *language);

const gchar *lightdm_language_get_name (LightDMLanguage *language);

const gchar *lightdm_language_get_territory (LightDMLanguage *language);

gboolean lightdm_language_matches (LightDMLanguage *language, const gchar *code);

G_END_DECLS

#endif /* LIGHTDM_LANGUAGE_H_ */
