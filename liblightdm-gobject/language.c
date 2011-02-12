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

#include <locale.h>
#include <langinfo.h>

#include "language.h"

enum {
    PROP_0,
    PROP_CODE,
    PROP_NAME,
    PROP_TERRITORY
};

struct _LdmLanguagePrivate
{
    gchar *code;
    gchar *name;
    gchar *territory;
};

G_DEFINE_TYPE (LdmLanguage, ldm_language, G_TYPE_OBJECT);

/**
 * ldm_language_new:
 * 
 * Create a new language.
 * @code: The language code
 * 
 * Return value: the new #LdmLanguage
 **/
LdmLanguage *
ldm_language_new (const gchar *code)
{
    return g_object_new (LDM_TYPE_LANGUAGE, "code", code, NULL);
}

/**
 * ldm_language_get_code:
 * @language: A #LdmLanguage
 * 
 * Get the code of a language.
 * 
 * Return value: The code of the language
 **/
const gchar *
ldm_language_get_code (LdmLanguage *language)
{
    return language->priv->code;
}

/**
 * ldm_language_get_name:
 * @language: A #LdmLanguage
 * 
 * Get the name of a language.
 *
 * Return value: The name of the language
 **/
const gchar *
ldm_language_get_name (LdmLanguage *language)
{
    if (!language->priv->name)
    {
        char *current = setlocale(LC_ALL, NULL);
        setlocale(LC_ALL, language->priv->code);
        language->priv->name = g_strdup (nl_langinfo (_NL_IDENTIFICATION_LANGUAGE));
        setlocale(LC_ALL, current);
    }

    return language->priv->name;
}

/**
 * ldm_language_get_territory:
 * @language: A #LdmLanguage
 * 
 * Get the territory the language is used in.
 * 
 * Return value: The territory the language is used in.
 **/
const gchar *
ldm_language_get_territory (LdmLanguage *language)
{
    if (!language->priv->territory)
    {
        char *current = setlocale(LC_ALL, NULL);
        setlocale(LC_ALL, language->priv->code);
        language->priv->territory = g_strdup (nl_langinfo (_NL_IDENTIFICATION_TERRITORY));
        setlocale(LC_ALL, current);
    }

    return language->priv->territory;
}

static gboolean
is_utf8 (const gchar *code)
{
   return g_str_has_suffix (code, ".utf8") || g_str_has_suffix (code, ".UTF-8");
}

/**
 * ldm_language_matches:
 * @language: A #LdmLanguage
 * @code: A language code
 * 
 * Check if a language code matches this language.
 * 
 * Return value: TRUE if the code matches this language.
 **/
gboolean
ldm_language_matches (LdmLanguage *language, const gchar *code)
{
    /* Handle the fact the UTF-8 is specified both as '.utf8' and '.UTF-8' */
    if (is_utf8 (language->priv->code) && is_utf8 (code))
    {
        /* Match the characters before the '.' */
        int i;
        for (i = 0; language->priv->code[i] && code[i] && language->priv->code[i] == code[i] && code[i] != '.' ; i++);
        return language->priv->code[i] == '.' && code[i] == '.';
    }

    return g_str_equal (language->priv->code, code);
}

static void
ldm_language_init (LdmLanguage *language)
{
    language->priv = G_TYPE_INSTANCE_GET_PRIVATE (language, LDM_TYPE_LANGUAGE, LdmLanguagePrivate);
}

static void
ldm_language_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LdmLanguage *self;
    gint i, n_pages;

    self = LDM_LANGUAGE (object);

    switch (prop_id) {
    case PROP_CODE:
        g_free (self->priv->name);
        self->priv->code = g_strdup (g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ldm_language_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LdmLanguage *self;

    self = LDM_LANGUAGE (object);

    switch (prop_id) {
    case PROP_CODE:
        g_value_set_string (value, ldm_language_get_code (self));
        break;
    case PROP_NAME:
        g_value_set_string (value, ldm_language_get_name (self));
        break;
    case PROP_TERRITORY:
        g_value_set_string (value, ldm_language_get_territory (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ldm_language_class_init (LdmLanguageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
    g_type_class_add_private (klass, sizeof (LdmLanguagePrivate));

    object_class->set_property = ldm_language_set_property;
    object_class->get_property = ldm_language_get_property;

    g_object_class_install_property(object_class,
                                    PROP_CODE,
                                    g_param_spec_string("code",
                                                        "code",
                                                        "Language code",
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property(object_class,
                                    PROP_NAME,
                                    g_param_spec_string("name",
                                                        "name",
                                                        "Name of the language",
                                                        NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_TERRITORY,
                                    g_param_spec_string("territory",
                                                        "territory",
                                                        "Territory the language is from",
                                                        NULL,
                                                        G_PARAM_READABLE));
}
