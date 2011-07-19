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

#include <string.h>
#include <locale.h>
#include <langinfo.h>

#include "lightdm/language.h"

enum {
    PROP_0,
    PROP_CODE,
    PROP_NAME,
    PROP_TERRITORY
};

typedef struct
{
    gchar *code;
    gchar *name;
    gchar *territory;
} LightDMLanguagePrivate;

G_DEFINE_TYPE (LightDMLanguage, lightdm_language, G_TYPE_OBJECT);

#define GET_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_LANGUAGE, LightDMLanguagePrivate)

static gboolean have_languages = FALSE;
static GList *languages = NULL;

static void
update_languages (void)
{
    gchar *stdout_text = NULL, *stderr_text = NULL;
    gint exit_status;
    gboolean result;
    GError *error = NULL;

    if (have_languages)
        return;

    result = g_spawn_command_line_sync ("locale -a", &stdout_text, &stderr_text, &exit_status, &error);
    if (!result || exit_status != 0)
        g_warning ("Failed to get languages, locale -a returned %d: %s", exit_status, error->message);
    else
    {
        gchar **tokens;
        int i;

        tokens = g_strsplit_set (stdout_text, "\n\r", -1);
        for (i = 0; tokens[i]; i++)
        {
            LightDMLanguage *language;
            gchar *code;

            code = g_strchug (tokens[i]);
            if (code[0] == '\0')
                continue;

            /* Ignore the non-interesting languages */
            if (strcmp (code, "C") == 0 || strcmp (code, "POSIX") == 0)
                continue;

            language = g_object_new (LIGHTDM_TYPE_LANGUAGE, "code", code, NULL);
            languages = g_list_append (languages, language);
        }

        g_strfreev (tokens);
    }

    g_clear_error (&error);
    g_free (stdout_text);
    g_free (stderr_text);

    have_languages = TRUE;
}

/**
 * lightdm_get_language:
 *
 * Get the current language.
 *
 * Return value: (transfer none): The current language or #NULL if no language.
 **/
const LightDMLanguage *
lightdm_get_language (void)
{
    const gchar *lang;
    GList *link;

    lang = g_getenv ("LANG");
    for (link = lightdm_get_languages (); link; link = link->next)
    {
        LightDMLanguage *language = link->data;
        if (lightdm_language_matches (language, lang))
            return language;
    }

    return NULL;
}

/**
 * lightdm_get_languages:
 *
 * Get a list of languages to present to the user.
 *
 * Return value: (element-type LightDMLanguage) (transfer none): A list of #LightDMLanguage that should be presented to the user.
 **/
GList *
lightdm_get_languages (void)
{
    update_languages ();
    return languages;
}

/**
 * lightdm_language_get_code:
 * @language: A #LightDMLanguage
 * 
 * Get the code of a language.
 * 
 * Return value: The code of the language
 **/
const gchar *
lightdm_language_get_code (LightDMLanguage *language)
{
    g_return_val_if_fail (LIGHTDM_IS_LANGUAGE (language), NULL);
    return GET_PRIVATE (language)->code;
}

/**
 * lightdm_language_get_name:
 * @language: A #LightDMLanguage
 * 
 * Get the name of a language.
 *
 * Return value: The name of the language
 **/
const gchar *
lightdm_language_get_name (LightDMLanguage *language)
{
    LightDMLanguagePrivate *priv;

    g_return_val_if_fail (LIGHTDM_IS_LANGUAGE (language), NULL);

    priv = GET_PRIVATE (language);

    if (!priv->name)
    {
        char *current = setlocale(LC_ALL, NULL);
        setlocale(LC_ALL, priv->code);
#ifdef _NL_IDENTIFICATION_LANGUAGE
        priv->name = g_strdup (nl_langinfo (_NL_IDENTIFICATION_LANGUAGE));
#else
        priv->name = g_strdup ("Unknown");
#endif
        setlocale(LC_ALL, current);
    }

    return priv->name;
}

/**
 * lightdm_language_get_territory:
 * @language: A #LightDMLanguage
 * 
 * Get the territory the language is used in.
 * 
 * Return value: The territory the language is used in.
 **/
const gchar *
lightdm_language_get_territory (LightDMLanguage *language)
{
    LightDMLanguagePrivate *priv;

    g_return_val_if_fail (LIGHTDM_IS_LANGUAGE (language), NULL);

    priv = GET_PRIVATE (language);

    if (!priv->territory)
    {
        char *current = setlocale(LC_ALL, NULL);
        setlocale(LC_ALL, priv->code);
#ifdef _NL_IDENTIFICATION_TERRITORY
        priv->territory = g_strdup (nl_langinfo (_NL_IDENTIFICATION_TERRITORY));
#else
        priv->territory = g_strdup ("Unknown");
#endif
        setlocale(LC_ALL, current);
    }

    return priv->territory;
}

static gboolean
is_utf8 (const gchar *code)
{
    return g_str_has_suffix (code, ".utf8") || g_str_has_suffix (code, ".UTF-8");
}

/**
 * lightdm_language_matches:
 * @language: A #LightDMLanguage
 * @code: A language code
 * 
 * Check if a language code matches this language.
 * 
 * Return value: #TRUE if the code matches this language.
 **/
gboolean
lightdm_language_matches (LightDMLanguage *language, const gchar *code)
{
    LightDMLanguagePrivate *priv;

    g_return_val_if_fail (LIGHTDM_IS_LANGUAGE (language), FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    priv = GET_PRIVATE (language);

    /* Handle the fact the UTF-8 is specified both as '.utf8' and '.UTF-8' */
    if (is_utf8 (priv->code) && is_utf8 (code))
    {
        /* Match the characters before the '.' */
        int i;
        for (i = 0; priv->code[i] && code[i] && priv->code[i] == code[i] && code[i] != '.' ; i++);
        return priv->code[i] == '.' && code[i] == '.';
    }

    return g_str_equal (priv->code, code);
}

static void
lightdm_language_init (LightDMLanguage *language)
{
}

static void
lightdm_language_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LightDMLanguage *self = LIGHTDM_LANGUAGE (object);
    LightDMLanguagePrivate *priv = GET_PRIVATE (self);

    switch (prop_id) {
    case PROP_CODE:
        g_free (priv->name);
        priv->code = g_strdup (g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_language_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LightDMLanguage *self;

    self = LIGHTDM_LANGUAGE (object);

    switch (prop_id) {
    case PROP_CODE:
        g_value_set_string (value, lightdm_language_get_code (self));
        break;
    case PROP_NAME:
        g_value_set_string (value, lightdm_language_get_name (self));
        break;
    case PROP_TERRITORY:
        g_value_set_string (value, lightdm_language_get_territory (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_language_class_init (LightDMLanguageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
    g_type_class_add_private (klass, sizeof (LightDMLanguagePrivate));

    object_class->set_property = lightdm_language_set_property;
    object_class->get_property = lightdm_language_get_property;

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
