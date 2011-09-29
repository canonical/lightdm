/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*-
 *
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

#include "accounts.h"
#include "dmrc.h"

struct UserPrivate
{
    /* Name of user */
    gchar *name;

    /* Accounts interface proxy */
    GDBusProxy *proxy;

    /* User ID */
    uid_t uid;

    /* Group ID */
    gid_t gid;

    /* GECOS information */
    gchar *gecos;

    /* Home directory */
    gchar *home_directory;

    /* Shell */
    gchar *shell;

    /* Language */
    gchar *language;

    /* X session */
    gchar *xsession;
};

G_DEFINE_TYPE (User, user, G_TYPE_OBJECT);

static GDBusProxy *accounts_proxy = NULL;
static gboolean have_accounts_proxy = FALSE;

static gboolean
call_method (GDBusProxy *proxy, const gchar *method, GVariant *args,
             const gchar *expected, GVariant **result)
{
    GVariant *answer;
    GError *error = NULL;

    if (!proxy)
        return FALSE;

    answer = g_dbus_proxy_call_sync (proxy,
                                     method,
                                     args,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    if (error)
        g_warning ("Could not call %s: %s", method, error->message);
    g_clear_error (&error);

    if (!answer)
        return FALSE;

    if (!g_variant_is_of_type (answer, G_VARIANT_TYPE (expected)))
    {
        g_warning ("Unexpected response from %s: %s",
                   method, g_variant_get_type_string (answer));
        g_variant_unref (answer);
        return FALSE;
    }

    if (result)
        *result = answer;
    else
        g_variant_unref (answer);

    return TRUE;
}

static gboolean
get_property (GDBusProxy *proxy, const gchar *property,
              const gchar *expected, GVariant **result)
{
    GVariant *answer;

    if (!proxy)
        return FALSE;

    answer = g_dbus_proxy_get_cached_property (proxy, property);

    if (!answer) {
        g_warning ("Could not get accounts property %s", property);
        return FALSE;
    }

    if (!g_variant_is_of_type (answer, G_VARIANT_TYPE (expected))) {
        g_warning ("Unexpected accounts property type for %s: %s",
                   property, g_variant_get_type_string (answer));
        g_variant_unref (answer);
        return FALSE;
    }

    if (result)
        *result = answer;
    else
        g_variant_unref (answer);
    return TRUE;
}

static void
save_string_to_dmrc (const gchar *username, const gchar *group,
                     const gchar *key, const gchar *value)
{
    GKeyFile *dmrc;

    dmrc = dmrc_load (username);
    g_key_file_set_string (dmrc, group, key, value);
    dmrc_save (dmrc, username);

    g_key_file_free (dmrc);
}

static gchar *
get_string_from_dmrc (const gchar *username, const gchar *group,
                      const gchar *key)
{
    GKeyFile *dmrc;
    gchar *value;

    dmrc = dmrc_load (username);
    value = g_key_file_get_string (dmrc, group, key, NULL);

    g_key_file_free (dmrc);
    return value;
}

static GDBusProxy *
get_accounts_proxy_for_user (const gchar *user)
{
    GError *error = NULL;
    GVariant *result;
    gboolean success;
    GDBusProxy *proxy;
    gchar *user_path = NULL;

    g_return_val_if_fail (user != NULL, NULL);  

    if (!have_accounts_proxy)
    {
        gchar *name;

        accounts_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                        G_DBUS_PROXY_FLAGS_NONE,
                                                        NULL,
                                                        "org.freedesktop.Accounts",
                                                        "/org/freedesktop/Accounts",
                                                        "org.freedesktop.Accounts",
                                                        NULL, &error);
        if (error)
            g_warning ("Could not get accounts proxy: %s", error->message);
        g_clear_error (&error);

        name = g_dbus_proxy_get_name_owner (accounts_proxy);
        if (!name)
        {
            g_debug ("org.freedesktop.Accounts does not exist, falling back to passwd file");
            g_object_unref (accounts_proxy);
            accounts_proxy = NULL;
        }
        g_free (name);

        have_accounts_proxy = TRUE;
    }

    if (!accounts_proxy)
        return NULL;

    success = call_method (accounts_proxy, "FindUserByName", g_variant_new ("(s)", user), "(o)", &result);

    if (!success)
        return NULL;

    g_variant_get (result, "(o)", &user_path);
    g_variant_unref (result);

    if (!user_path)
        return NULL;

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.Accounts",
                                           user_path,
                                           "org.freedesktop.Accounts.User",
                                           NULL, &error);
    if (error)
        g_warning ("Could not get accounts user proxy: %s", error->message);
    g_clear_error (&error);
    g_free (user_path);

    return proxy;
}

static User *
user_from_passwd (struct passwd *user_info)
{
    User *user;

    user = g_object_new (USER_TYPE, NULL);
    user->priv->name = g_strdup (user_info->pw_name);
    user->priv->uid = user_info->pw_uid;
    user->priv->gid = user_info->pw_gid;
    user->priv->gecos = g_strdup (user_info->pw_gecos);
    user->priv->home_directory = g_strdup (user_info->pw_dir);
    user->priv->shell = g_strdup (user_info->pw_shell);
    user->priv->proxy = get_accounts_proxy_for_user (user->priv->name);

    return user;
}

User *
accounts_get_user_by_name (const gchar *username)
{
    struct passwd *user_info;
    User *user = NULL;

    g_return_val_if_fail (username != NULL, NULL);

    errno = 0;
    user_info = getpwnam (username);
    if (user_info)
        user = user_from_passwd (user_info);

    if (!user && errno != 0)
        g_warning ("Unable to get information on user %s: %s", username, strerror (errno));

    return user;
}

User *
accounts_get_user_by_uid (uid_t uid)
{
    User *user = NULL;

    errno = 0;
    struct passwd *user_info;

    user_info = getpwuid (uid);
    if (user_info)
        user = user_from_passwd (user_info);

    if (!user && errno != 0)
        g_warning ("Unable to get information on user %d: %s", uid, strerror (errno));

    return user;
}

User *
accounts_get_current_user ()
{
    return user_from_passwd (getpwuid (getuid ()));
}

const gchar *
user_get_name (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return user->priv->name;
}

uid_t
user_get_uid (User *user)
{
    g_return_val_if_fail (user != NULL, 0);
    return user->priv->uid;
}

gid_t
user_get_gid (User *user)
{
    g_return_val_if_fail (user != NULL, 0);
    return user->priv->gid;
}

const gchar *
user_get_gecos (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return user->priv->gecos;
}

const gchar *
user_get_home_directory (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return user->priv->home_directory;
}

const gchar *
user_get_shell (User *user)
{
    g_return_val_if_fail (user != NULL, NULL);
    return user->priv->shell;
}

void
user_set_language (User *user, const gchar *language)
{
    g_return_if_fail (user != NULL);

    call_method (user->priv->proxy, "SetLanguage", g_variant_new ("(s)", language), "()", NULL);
    save_string_to_dmrc (user->priv->name, "Desktop", "Language", language);
}

const gchar *
user_get_language (User *user)
{
    GVariant *result;

    g_return_val_if_fail (user != NULL, NULL);

    g_free (user->priv->language);

    if (get_property (user->priv->proxy, "Language", "s", &result))
    {
        g_variant_get (result, "s", &user->priv->language);
        g_variant_unref (result);
    }
    else
        user->priv->language = get_string_from_dmrc (user->priv->name, "Desktop", "Language");

    if (g_strcmp0 (user->priv->language, "") == 0)
    {
        g_free (user->priv->language);
        user->priv->language = NULL;
    }

    return user->priv->language;
}

void
user_set_xsession (User *user, const gchar *xsession)
{
    g_return_if_fail (user != NULL);

    call_method (user->priv->proxy, "SetXSession", g_variant_new ("(s)", xsession), "()", NULL);
    save_string_to_dmrc (user->priv->name, "Desktop", "Session", xsession);
}

const gchar *
user_get_xsession (User *user)
{
    GVariant *result;

    g_return_val_if_fail (user != NULL, NULL);

    g_free (user->priv->xsession);

    if (get_property (user->priv->proxy, "XSession", "s", &result))
    {
        g_variant_get (result, "s", &user->priv->xsession);
        g_variant_unref (result);
    }
    else
        user->priv->xsession = get_string_from_dmrc (user->priv->name, "Desktop", "Session");

    if (g_strcmp0 (user->priv->xsession, "") == 0)
    {
        g_free (user->priv->xsession);
        user->priv->xsession = NULL;
    }

    return user->priv->xsession;
}

static void
user_init (User *user)
{
    user->priv = G_TYPE_INSTANCE_GET_PRIVATE (user, USER_TYPE, UserPrivate);
}

static void
user_dispose (GObject *object)
{
    User *self;

    self = USER (object);

    if (self->priv->proxy) {
        g_object_unref (self->priv->proxy);
        self->priv->proxy = NULL;
    }

    G_OBJECT_CLASS (user_parent_class)->dispose (object);
}

static void
user_finalize (GObject *object)
{
    User *self;

    self = USER (object);

    g_free (self->priv->name);
    g_free (self->priv->gecos);
    g_free (self->priv->home_directory);
    g_free (self->priv->shell);

    G_OBJECT_CLASS (user_parent_class)->finalize (object);  
}

static void
user_class_init (UserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = user_dispose;
    object_class->finalize = user_finalize;  

    g_type_class_add_private (klass, sizeof (UserPrivate));
}
