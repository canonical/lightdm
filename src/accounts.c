/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*-
 *
 * Copyright (C) 2011 Canonical Ltd
 * Author: Michael Terry <michael.terry@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include "accounts.h"
#include "dmrc.h"
#include <gio/gio.h>

struct AccountsPrivate
{
    gchar      *username;
    GDBusProxy *proxy;
};

G_DEFINE_TYPE (Accounts, accounts, G_TYPE_OBJECT);

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

    if (!answer) {
        g_warning ("Could not call %s: %s", method, error->message);
        g_error_free (error);
        return FALSE;
    }

    if (!g_variant_is_of_type (answer, G_VARIANT_TYPE (expected))) {
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

Accounts *
accounts_new (const gchar *user)
{
    g_return_val_if_fail (user != NULL, NULL);

    Accounts *accounts;
    GDBusProxy *proxy;
    GError *error = NULL;
    GVariant *result;
    gboolean success;
    gchar *user_path = NULL;

    accounts = g_object_new (ACCOUNTS_TYPE, NULL);
    accounts->priv->username = g_strdup (user);

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.Accounts",
                                           "/org/freedesktop/Accounts",
                                           "org.freedesktop.Accounts",
                                           NULL, &error);

    if (!proxy) {
        g_warning ("Could not get accounts proxy: %s", error->message);
        g_error_free (error);
        return accounts;
    }

    success = call_method (proxy, "FindUserByName", g_variant_new ("(s)", user),
                           "(o)", &result);
    g_object_unref (proxy);

    if (!success)
        return accounts;

    g_variant_get (result, "(o)", &user_path);
    g_variant_unref (result);

    if (!user_path)
        return accounts;

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           NULL,
                                           "org.freedesktop.Accounts",
                                           user_path,
                                           "org.freedesktop.Accounts.User",
                                           NULL, &error);
    g_free (user_path);

    if (!proxy) {
        g_warning ("Could not get accounts user proxy: %s", error->message);
        g_error_free (error);
        return accounts;
    }

    accounts->priv->proxy = proxy;
    return accounts;
}

void
accounts_set_session (Accounts *accounts, const gchar *session)
{
    g_return_if_fail (accounts != NULL);

    call_method (accounts->priv->proxy, "SetXSession",
                 g_variant_new ("(s)", session), "()", NULL);

    save_string_to_dmrc (accounts->priv->username, "Desktop", "Session", session);
}

gchar *
accounts_get_session (Accounts *accounts)
{
    g_return_val_if_fail (accounts != NULL, NULL);

    GVariant *result;
    gchar *session;

    if (!get_property (accounts->priv->proxy, "XSession",
                       "s", &result))
        return get_string_from_dmrc (accounts->priv->username, "Desktop", "Session");

    g_variant_get (result, "s", &session);
    g_variant_unref (result);

    if (g_strcmp0 (session, "") == 0) {
        g_free (session);
        return NULL;
    }

    return session;
}

static void
accounts_init (Accounts *accounts)
{
    accounts->priv = G_TYPE_INSTANCE_GET_PRIVATE (accounts, ACCOUNTS_TYPE, AccountsPrivate);
}

static void
accounts_dispose (GObject *object)
{
    Accounts *self;

    self = ACCOUNTS (object);

    if (self->priv->proxy) {
        g_object_unref (self->priv->proxy);
        self->priv->proxy = NULL;
    }

    G_OBJECT_CLASS (accounts_parent_class)->dispose (object);  
}

static void
accounts_finalize (GObject *object)
{
    Accounts *self;

    self = ACCOUNTS (object);

    if (self->priv->username) {
        g_free (self->priv->username);
        self->priv->username = NULL;
    }

    G_OBJECT_CLASS (accounts_parent_class)->finalize (object);  
}

static void
accounts_class_init (AccountsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = accounts_dispose;
    object_class->finalize = accounts_finalize;

    g_type_class_add_private (klass, sizeof (AccountsPrivate));
}
