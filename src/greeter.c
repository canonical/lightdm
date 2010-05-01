/*
 * Copyright (C) 2010 Canonical Ltd.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <errno.h>
#include <dbus/dbus-glib.h>
#include <security/pam_appl.h>
#include <string.h>
#include <pwd.h>

#include "greeter.h"

enum {
    SHOW_PROMPT,
    SHOW_MESSAGE,
    SHOW_ERROR,
    AUTHENTICATION_COMPLETE,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct GreeterPrivate
{
    DBusGConnection *bus;

    DBusGProxy *proxy;

    gboolean have_users;
    GList *users;

    gboolean is_authenticated;
};

G_DEFINE_TYPE (Greeter, greeter, G_TYPE_OBJECT);

Greeter *
greeter_new (/*int argc, char **argv*/)
{
    /*if (argc != 2)
    {
        g_warning ("Incorrect arguments provided to Greeter");
        return NULL;
    }*/

    return g_object_new (GREETER_TYPE, /*"?", argv[1],*/ NULL);
}

gboolean
greeter_connect (Greeter *greeter)
{
    gchar *timed_user;
    gint login_delay;
    gboolean result;
    GError *error = NULL;

    result = dbus_g_proxy_call (greeter->priv->proxy, "Connect", &error,
                                G_TYPE_INVALID,
                                G_TYPE_STRING, &timed_user,
                                G_TYPE_INT, &login_delay,
                                G_TYPE_INVALID);

    if (!result)
        g_warning ("Failed to connect to display manager: %s", error->message);
    g_clear_error (&error);

    return result;
}

// FIXME: 100 or 500? Make it configurable
#define MINIMUM_UID 500

static gint
compare_user (gconstpointer a, gconstpointer b)
{
    const UserInfo *user_a = a, *user_b = b;
    const char *name_a, *name_b;
    name_a = user_a->real_name ? user_a->real_name : user_a->name;
    name_b = user_b->real_name ? user_b->real_name : user_b->name;
    return strcmp (name_a, name_b);
}

static void
update_users (Greeter *greeter)
{
    char *invalid_shells[] = { "/bin/false", "/usr/sbin/nologin", NULL };
    char *invalid_users[] = { "nobody", NULL };

    setpwent ();

    while (TRUE)
    {
        struct passwd *entry;
        UserInfo *user;
        char **tokens;
        int i;

        errno = 0;
        entry = getpwent ();
        if (!entry)
            break;

        /* Ignore system users */
        if (entry->pw_uid < MINIMUM_UID)
            continue;

        /* Ignore users disabled by shell */
        if (entry->pw_shell)
        {
            for (i = 0; invalid_shells[i] && strcmp (entry->pw_shell, invalid_shells[i]) != 0; i++);
            if (invalid_shells[i])
                continue;
        }

        /* Ignore certain users */
        for (i = 0; invalid_users[i] && strcmp (entry->pw_name, invalid_users[i]) != 0; i++);
        if (invalid_users[i])
            continue;

        user = g_malloc0 (sizeof (UserInfo));
        user->name = g_strdup (entry->pw_name);

        tokens = g_strsplit (entry->pw_gecos, ",", -1);
        if (tokens[0] != NULL && tokens[0][0] != '\0')
            user->real_name = g_strdup (tokens[0]);
        else
            user->real_name = NULL;
        g_strfreev (tokens);

        greeter->priv->users = g_list_insert_sorted (greeter->priv->users, user, compare_user);
    }

    if (errno != 0)
        g_warning ("Failed to read password database: %s", strerror (errno));

    endpwent ();
}

gint
greeter_get_num_users (Greeter *greeter)
{
    if (!greeter->priv->have_users)
        update_users (greeter);
    return g_list_length (greeter->priv->users);
}

const GList *
greeter_get_users (Greeter *greeter)
{
    if (!greeter->priv->have_users)
        update_users (greeter);
    return greeter->priv->users;
}

#define DBUS_STRUCT_INT_STRING dbus_g_type_get_struct ("GValueArray", G_TYPE_INT, G_TYPE_STRING, G_TYPE_INVALID)
#define DBUS_ARRAY_STRUCT_INT_STRING dbus_g_type_get_collection ("GPtrArray", DBUS_STRUCT_INT_STRING)

static void
auth_response_cb (DBusGProxy *proxy, DBusGProxyCall *call, gpointer userdata)
{
    Greeter *greeter = userdata;
    gboolean result;
    GError *error = NULL;
    gint return_code;
    GPtrArray *array;
    int i;

    result = dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INT, &return_code, DBUS_ARRAY_STRUCT_INT_STRING, &array, G_TYPE_INVALID);
    if (!result)
        g_warning ("Failed to complete D-Bus call: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    for (i = 0; i < array->len; i++)
    {
        GValue value = { 0 };
        gint msg_style;
        gchar *msg;
      
        g_value_init (&value, DBUS_STRUCT_INT_STRING);
        g_value_set_static_boxed (&value, array->pdata[i]);
        dbus_g_type_struct_get (&value, 0, &msg_style, 1, &msg, G_MAXUINT);

        // FIXME: Should stop on prompts?
        switch (msg_style)
        {
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON:
            g_signal_emit (G_OBJECT (greeter), signals[SHOW_PROMPT], 0, msg);
            break;
        case PAM_ERROR_MSG:
            g_signal_emit (G_OBJECT (greeter), signals[SHOW_ERROR], 0, msg);
            break;
        case PAM_TEXT_INFO:
            g_signal_emit (G_OBJECT (greeter), signals[SHOW_MESSAGE], 0, msg);
            break;
        }

        g_free (msg);

        g_value_unset (&value);
    }

    if (array->len == 0)
    {
        greeter->priv->is_authenticated = (return_code == 0);
        g_signal_emit (G_OBJECT (greeter), signals[AUTHENTICATION_COMPLETE], 0);
    }

    g_ptr_array_unref (array);
}

void
greeter_start_authentication (Greeter *greeter, const char *username)
{
    dbus_g_proxy_begin_call (greeter->priv->proxy, "StartAuthentication", auth_response_cb, greeter, NULL, G_TYPE_STRING, username, G_TYPE_INVALID);
}

void
greeter_provide_secret (Greeter *greeter, const gchar *secret)
{
    gchar **secrets;

    // FIXME: Could be multiple secrets required
    secrets = g_malloc (sizeof (char *) * 2);
    secrets[0] = g_strdup (secret);
    secrets[1] = NULL;
    dbus_g_proxy_begin_call (greeter->priv->proxy, "ContinueAuthentication", auth_response_cb, greeter, NULL, G_TYPE_STRV, secrets, G_TYPE_INVALID);
}

void
greeter_cancel_authentication (Greeter *greeter)
{
}

gboolean
greeter_get_is_authenticated (Greeter *greeter)
{
    return greeter->priv->is_authenticated;
}

static void
greeter_init (Greeter *greeter)
{
    GError *error = NULL;

    greeter->priv = G_TYPE_INSTANCE_GET_PRIVATE (greeter, GREETER_TYPE, GreeterPrivate);
  
    greeter->priv->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
    if (!greeter->priv->bus)
        g_error ("Failed to connect to bus: %s", error->message);
    g_clear_error (&error);

    greeter->priv->proxy = dbus_g_proxy_new_for_name (greeter->priv->bus,
                                                      "org.gnome.LightDisplayManager",
                                                      "/org/gnome/LightDisplayManager",/* FIXME: /Display", */
                                                      "org.gnome.LightDisplayManager.Display");
}

static void
greeter_class_init (GreeterClass *klass)
{
    g_type_class_add_private (klass, sizeof (GreeterPrivate));

    signals[SHOW_PROMPT] =
        g_signal_new ("show-prompt",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, show_prompt),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
    signals[SHOW_MESSAGE] =
        g_signal_new ("show-message",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, show_message),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
    signals[SHOW_ERROR] =
        g_signal_new ("show-error",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, show_error),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
    signals[AUTHENTICATION_COMPLETE] =
        g_signal_new ("authentication-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, authentication_complete),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
