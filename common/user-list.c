/* -*- Mode: C; indent-tabs-mode:nil; tab-width:4 -*-
 *
 * Copyright (C) 2010 Robert Ancell.
 * Copyright (C) 2014 Canonical, Ltd.
 * Authors: Robert Ancell <robert.ancell@canonical.com>
 *          Michael Terry <michael.terry@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include <config.h>

#include <errno.h>
#include <string.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <gio/gio.h>

#include "dmrc.h"
#include "user-list.h"

enum
{
    LIST_PROP_NUM_USERS = 1,
    LIST_PROP_USERS,
};

enum
{
    USER_PROP_NAME = 1,
    USER_PROP_REAL_NAME,
    USER_PROP_DISPLAY_NAME,
    USER_PROP_HOME_DIRECTORY,
    USER_PROP_SHELL,
    USER_PROP_IMAGE,
    USER_PROP_BACKGROUND,
    USER_PROP_LANGUAGE,
    USER_PROP_LAYOUT,
    USER_PROP_LAYOUTS,
    USER_PROP_SESSION,
    USER_PROP_LOGGED_IN,
    USER_PROP_HAS_MESSAGES,
    USER_PROP_UID,
    USER_PROP_GID,
    USER_PROP_IS_LOCKED,
};

enum
{
    USER_ADDED,
    USER_CHANGED,
    USER_REMOVED,
    LAST_LIST_SIGNAL
};
static guint list_signals[LAST_LIST_SIGNAL] = { 0 };

enum
{
    CHANGED,
    GET_LOGGED_IN,
    LAST_USER_SIGNAL
};
static guint user_signals[LAST_USER_SIGNAL] = { 0 };

typedef struct
{
    /* Bus connection being communicated on */
    GDBusConnection *bus;

    /* D-Bus signals for accounts service events */
    guint user_added_signal;
    guint user_removed_signal;

    /* D-Bus signals for display manager events */
    guint session_added_signal;
    guint session_removed_signal;

    /* File monitor for password file */
    GFileMonitor *passwd_monitor;

    /* TRUE if have scanned users */
    gboolean have_users;

    /* List of users */
    GList *users;

    /* List of sessions */
    GList *sessions;
} CommonUserListPrivate;

typedef struct
{
    /* TRUE if have loaded the DMRC file */
    gboolean loaded_dmrc;

    /* Bus we are listening for accounts service on */
    GDBusConnection *bus;

    /* Accounts service path */
    gchar *path;

    /* Update signal from accounts service */
    guint changed_signal;

    /* Username */
    gchar *name;

    /* Descriptive name for user */
    gchar *real_name;

    /* Home directory of user */
    gchar *home_directory;

    /* Shell for user */
    gchar *shell;

    /* Image for user */
    gchar *image;

    /* Background image for users */
    gchar *background;

    /* TRUE if this user has messages available */
    gboolean has_messages;

    /* UID of user */
    guint64 uid;

    /* GID of user */
    guint64 gid;

    /* User chosen language */
    gchar *language;

    /* User layout preferences */
    gchar **layouts;

    /* User default session */
    gchar *session;

    /* TRUE if this user is locked */
    gboolean is_locked;
} CommonUserPrivate;

typedef struct
{
    GObject parent_instance;
    gchar *path;
    gchar *username;
} CommonSession;

typedef struct
{
    GObjectClass parent_class;
} CommonSessionClass;

G_DEFINE_TYPE_WITH_PRIVATE (CommonUserList, common_user_list, G_TYPE_OBJECT)
G_DEFINE_TYPE_WITH_PRIVATE (CommonUser, common_user, G_TYPE_OBJECT)
#define COMMON_SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), common_session_get_type (), CommonSession))
GType common_session_get_type (void);
G_DEFINE_TYPE (CommonSession, common_session, G_TYPE_OBJECT)

#define PASSWD_FILE      "/etc/passwd"
#define USER_CONFIG_FILE "/etc/lightdm/users.conf"

static CommonUserList *singleton = NULL;

/**
 * common_user_list_get_instance:
 *
 * Get the user list.
 *
 * Return value: (transfer none): the #CommonUserList
 **/
CommonUserList *
common_user_list_get_instance (void)
{
    if (!singleton)
        singleton = g_object_new (COMMON_TYPE_USER_LIST, NULL);
    return singleton;
}

void
common_user_list_cleanup (void)
{
    g_clear_object (&singleton);
}

static CommonUser *
get_user_by_name (CommonUserList *user_list, const gchar *username)
{
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);

    for (GList *link = priv->users; link; link = link->next)
    {
        CommonUser *user = link->data;
        if (g_strcmp0 (common_user_get_name (user), username) == 0)
            return user;
    }

    return NULL;
}

static CommonUser *
get_user_by_path (CommonUserList *user_list, const gchar *path)
{
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);

    for (GList *link = priv->users; link; link = link->next)
    {
        CommonUser *user = link->data;
        CommonUserPrivate *user_priv = common_user_get_instance_private (user);
        if (g_strcmp0 (user_priv->path, path) == 0)
            return user;
    }

    return NULL;
}

static gint
compare_user (gconstpointer a, gconstpointer b)
{
    CommonUser *user_a = (CommonUser *) a, *user_b = (CommonUser *) b;
    return g_strcmp0 (common_user_get_display_name (user_a), common_user_get_display_name (user_b));
}

static gboolean
update_passwd_user (CommonUser *user, const gchar *real_name, const gchar *home_directory, const gchar *shell, const gchar *image)
{
    CommonUserPrivate *priv = common_user_get_instance_private (user);

    /* Skip if already set to this */
    if (g_strcmp0 (common_user_get_real_name (user), real_name) == 0 &&
        g_strcmp0 (common_user_get_home_directory (user), home_directory) == 0 &&
        g_strcmp0 (common_user_get_shell (user), shell) == 0 &&
        g_strcmp0 (common_user_get_image (user), image) == 0)
        return FALSE;

    g_free (priv->real_name);
    priv->real_name = g_strdup (real_name);
    g_free (priv->home_directory);
    priv->home_directory = g_strdup (home_directory);
    g_free (priv->shell);
    priv->shell = g_strdup (shell);
    g_free (priv->image);
    priv->image = g_strdup (image);

    return TRUE;
}

static void load_sessions (CommonUserList *user_list);

static gboolean
get_logged_in_cb (CommonUser *user, CommonUserList *user_list)
{
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);
    CommonUserPrivate *user_priv = common_user_get_instance_private (user);

    // Lazily decide to load/listen to sessions
    if (priv->session_added_signal == 0)
        load_sessions (user_list);

    const gchar *username = user_priv->name;
    for (GList *link = priv->sessions; link; link = link->next)
    {
        CommonSession *session = link->data;
        if (strcmp (session->username, username) == 0)
            return TRUE;
    }

    return FALSE;
}

static void
user_changed_cb (CommonUser *user, CommonUserList *user_list)
{
    g_signal_emit (user_list, list_signals[USER_CHANGED], 0, user);
}

static CommonUser *
make_passwd_user (CommonUserList *user_list, struct passwd *entry)
{
    CommonUser *user = g_object_new (COMMON_TYPE_USER, NULL);
    CommonUserPrivate *priv = common_user_get_instance_private (user);

    g_signal_connect (user, "get-logged-in", G_CALLBACK (get_logged_in_cb), user_list);

    g_auto(GStrv) tokens = g_strsplit (entry->pw_gecos, ",", -1);
    gchar *real_name;
    if (tokens[0] != NULL && tokens[0][0] != '\0')
        real_name = g_strdup (tokens[0]);
    else
        real_name = g_strdup ("");

    gchar *image = g_build_filename (entry->pw_dir, ".face", NULL);
    if (!g_file_test (image, G_FILE_TEST_EXISTS))
    {
        g_free (image);
        image = g_build_filename (entry->pw_dir, ".face.icon", NULL);
        if (!g_file_test (image, G_FILE_TEST_EXISTS))
        {
            g_free (image);
            image = NULL;
        }
    }

    priv->name = g_strdup (entry->pw_name);
    priv->real_name = real_name;
    priv->home_directory = g_strdup (entry->pw_dir);
    priv->shell = g_strdup (entry->pw_shell);
    priv->image = image;
    priv->uid = entry->pw_uid;
    priv->gid = entry->pw_gid;

    return user;
}

static void
load_passwd_file (CommonUserList *user_list, gboolean emit_add_signal)
{
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);

    g_debug ("Loading user config from %s", USER_CONFIG_FILE);

    g_autoptr(GKeyFile) config = g_key_file_new ();
    g_autoptr(GError) error = NULL;
    g_key_file_load_from_file (config, USER_CONFIG_FILE, G_KEY_FILE_NONE, &error);
    if (error && !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Failed to load configuration from %s: %s", USER_CONFIG_FILE, error->message);

    gint minimum_uid = 500;
    if (g_key_file_has_key (config, "UserList", "minimum-uid", NULL))
        minimum_uid = g_key_file_get_integer (config, "UserList", "minimum-uid", NULL);

    g_autofree gchar *hidden_users_list = g_key_file_get_string (config, "UserList", "hidden-users", NULL);
    if (!hidden_users_list)
        hidden_users_list = g_strdup ("nobody nobody4 noaccess");
    g_auto(GStrv) hidden_users = g_strsplit (hidden_users_list, " ", -1);

    g_autofree gchar *hidden_shells_list = g_key_file_get_string (config, "UserList", "hidden-shells", NULL);
    if (!hidden_shells_list)
        hidden_shells_list = g_strdup ("/bin/false /usr/sbin/nologin");
    g_auto(GStrv) hidden_shells = g_strsplit (hidden_shells_list, " ", -1);

    setpwent ();

    GList *users = NULL, *new_users = NULL, *changed_users = NULL;
    while (TRUE)
    {
        errno = 0;
        struct passwd *entry = getpwent ();
        if (!entry)
            break;

        /* Ignore system users */
        if (entry->pw_uid < minimum_uid)
            continue;

        /* Ignore users disabled by shell */
        if (entry->pw_shell)
        {
            int i;
            for (i = 0; hidden_shells[i] && strcmp (entry->pw_shell, hidden_shells[i]) != 0; i++);
            if (hidden_shells[i])
                continue;
        }

        /* Ignore certain users */
        int i;
        for (i = 0; hidden_users[i] && strcmp (entry->pw_name, hidden_users[i]) != 0; i++);
        if (hidden_users[i])
            continue;

        CommonUser *user = make_passwd_user (user_list, entry);

        /* Update existing users if have them */
        GList *link;
        for (link = priv->users; link; link = link->next)
        {
            CommonUser *info = link->data;
            if (strcmp (common_user_get_name (info), common_user_get_name (user)) == 0)
            {
                if (update_passwd_user (info, common_user_get_real_name (user), common_user_get_home_directory (user), common_user_get_shell (user), common_user_get_image (user)))
                    changed_users = g_list_insert_sorted (changed_users, info, compare_user);
                g_object_unref (user);
                user = info;
                break;
            }
        }
        if (!link)
        {
            /* Only notify once we have loaded the user list */
            if (priv->have_users)
                new_users = g_list_insert_sorted (new_users, user, compare_user);
        }
        users = g_list_insert_sorted (users, user, compare_user);
    }

    if (errno != 0)
        g_warning ("Failed to read password database: %s", strerror (errno));

    endpwent ();

    /* Use new user list */
    GList *old_users = priv->users;
    priv->users = users;

    /* Notify of changes */
    for (GList *link = new_users; link; link = link->next)
    {
        CommonUser *info = link->data;
        g_debug ("User %s added", common_user_get_name (info));
        g_signal_connect (info, USER_SIGNAL_CHANGED, G_CALLBACK (user_changed_cb), user_list);
        if (emit_add_signal)
            g_signal_emit (user_list, list_signals[USER_ADDED], 0, info);
    }
    g_list_free (new_users);
    for (GList *link = changed_users; link; link = link->next)
    {
        CommonUser *info = link->data;
        g_debug ("User %s changed", common_user_get_name (info));
        g_signal_emit (info, user_signals[CHANGED], 0);
    }
    g_list_free (changed_users);
    for (GList *link = old_users; link; link = link->next)
    {
        /* See if this user is in the current list */
        GList *new_link;
        for (new_link = priv->users; new_link; new_link = new_link->next)
        {
            if (new_link->data == link->data)
                break;
        }

        if (!new_link)
        {
            CommonUser *info = link->data;
            g_debug ("User %s removed", common_user_get_name (info));
            g_signal_emit (user_list, list_signals[USER_REMOVED], 0, info);
            g_object_unref (info);
        }
    }
    g_list_free (old_users);
}

static void
passwd_changed_cb (GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, CommonUserList *user_list)
{
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
        g_debug ("%s changed, reloading user list", g_file_get_path (file));
        load_passwd_file (user_list, TRUE);
    }
}

static gboolean load_accounts_user (CommonUser *user);

static void
accounts_user_changed_cb (GDBusConnection *connection,
                          const gchar *sender_name,
                          const gchar *object_path,
                          const gchar *interface_name,
                          const gchar *signal_name,
                          GVariant *parameters,
                          gpointer data)
{
    CommonUser *user = data;
    /*CommonUserPrivate *priv = common_user_get_instance_private (user);*/

    /* Log message disabled as AccountsService can have arbitrary plugins that
     * might cause us to log when properties change we don't use. LP: #1376357
     */
    /*g_debug ("User %s changed", priv->path);*/
    if (load_accounts_user (user))
        g_signal_emit (user, user_signals[CHANGED], 0);
}

static gboolean
load_accounts_user (CommonUser *user)
{
    CommonUserPrivate *priv = common_user_get_instance_private (user);

    /* Get the properties for this user */
    if (!priv->changed_signal)
        priv->changed_signal = g_dbus_connection_signal_subscribe (priv->bus,
                                                                   "org.freedesktop.Accounts",
                                                                   "org.freedesktop.Accounts.User",
                                                                   "Changed",
                                                                   priv->path,
                                                                   NULL,
                                                                   G_DBUS_SIGNAL_FLAGS_NONE,
                                                                   accounts_user_changed_cb,
                                                                   user,
                                                                   NULL);

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (priv->bus,
                                                              "org.freedesktop.Accounts",
                                                              priv->path,
                                                              "org.freedesktop.DBus.Properties",
                                                              "GetAll",
                                                              g_variant_new ("(s)", "org.freedesktop.Accounts.User"),
                                                              G_VARIANT_TYPE ("(a{sv})"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Error updating user %s: %s", priv->path, error->message);
    if (!result)
        return FALSE;

    /* Store the properties we need */
    g_autoptr(GVariantIter) iter = NULL;
    g_variant_get (result, "(a{sv})", &iter);
    const gchar *name;
    GVariant *value;
    gboolean system_account = FALSE;
    while (g_variant_iter_loop (iter, "{&sv}", &name, &value))
    {
        if (strcmp (name, "UserName") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->name);
            priv->name = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "RealName") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->real_name);
            priv->real_name = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "HomeDirectory") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->home_directory);
            priv->home_directory = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "Shell") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->shell);
            priv->shell = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "SystemAccount") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            system_account = g_variant_get_boolean (value);
        else if (strcmp (name, "Language") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            if (priv->language)
                g_free (priv->language);
            priv->language = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "IconFile") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->image);
            priv->image = g_variant_dup_string (value, NULL);
            if (strcmp (priv->image, "") == 0)
                g_clear_pointer (&priv->image, g_free);
        }
        else if (strcmp (name, "XSession") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        {
            g_free (priv->session);
            priv->session = g_variant_dup_string (value, NULL);
        }
        else if (strcmp (name, "Uid") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
            priv->uid = g_variant_get_uint64 (value);
        else if (strcmp (name, "Locked") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
            priv->is_locked = g_variant_get_boolean (value);
    }

    g_autoptr(GVariant) extra_result = g_dbus_connection_call_sync (priv->bus,
                                                                    "org.freedesktop.Accounts",
                                                                    priv->path,
                                                                    "org.freedesktop.DBus.Properties",
                                                                    "GetAll",
                                                                    g_variant_new ("(s)", "org.freedesktop.DisplayManager.AccountsService"),
                                                                    G_VARIANT_TYPE ("(a{sv})"),
                                                                    G_DBUS_CALL_FLAGS_NONE,
                                                                    -1,
                                                                    NULL,
                                                                    &error);
    if (error)
        g_warning ("Error updating user %s: %s", priv->path, error->message);
    if (extra_result) {
        g_autoptr(GVariantIter) extra_iter = NULL;

        g_variant_get (extra_result, "(a{sv})", &extra_iter);
        while (g_variant_iter_loop (extra_iter, "{&sv}", &name, &value))
        {
            if (strcmp (name, "BackgroundFile") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
            {
                g_free (priv->background);
                priv->background = g_variant_dup_string (value, NULL);
                if (strcmp (priv->background, "") == 0)
                    g_clear_pointer (&priv->background, g_free);
            }
            else if (strcmp (name, "HasMessages") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
                priv->has_messages = g_variant_get_boolean (value);
            else if (strcmp (name, "KeyboardLayouts") == 0 && g_variant_is_of_type (value, G_VARIANT_TYPE_STRING_ARRAY))
            {
                g_strfreev (priv->layouts);
                priv->layouts = g_variant_dup_strv (value, NULL);
                if (!priv->layouts)
                {
                    priv->layouts = g_malloc (sizeof (gchar *) * 1);
                    priv->layouts[0] = NULL;
                }
            }
        }
    }

    return !system_account;
}

static void
add_accounts_user (CommonUserList *user_list, const gchar *path, gboolean emit_signal)
{
    CommonUserListPrivate *list_priv = common_user_list_get_instance_private (user_list);

    CommonUser *user = g_object_new (COMMON_TYPE_USER, NULL);
    CommonUserPrivate *priv = common_user_get_instance_private (user);

    g_debug ("User %s added", path);
    priv->bus = g_object_ref (list_priv->bus);
    priv->path = g_strdup (path);
    g_signal_connect (user, USER_SIGNAL_CHANGED, G_CALLBACK (user_changed_cb), user_list);
    g_signal_connect (user, "get-logged-in", G_CALLBACK (get_logged_in_cb), user_list);
    if (load_accounts_user (user))
    {
        list_priv->users = g_list_insert_sorted (list_priv->users, user, compare_user);
        if (emit_signal)
            g_signal_emit (user_list, list_signals[USER_ADDED], 0, user);
    }
    else
        g_object_unref (user);
}

static void
accounts_user_added_cb (GDBusConnection *connection,
                        const gchar *sender_name,
                        const gchar *object_path,
                        const gchar *interface_name,
                        const gchar *signal_name,
                        GVariant *parameters,
                        gpointer data)
{
    CommonUserList *user_list = data;

    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(o)")))
    {
        g_warning ("Got UserAccounts signal UserAdded with unknown parameters %s", g_variant_get_type_string (parameters));
        return;
    }

    const gchar *path;
    g_variant_get (parameters, "(&o)", &path);

    /* Add user if we haven't got them */
    CommonUser *user = get_user_by_path (user_list, path);
    if (!user)
        add_accounts_user (user_list, path, TRUE);
}

static void
accounts_user_deleted_cb (GDBusConnection *connection,
                          const gchar *sender_name,
                          const gchar *object_path,
                          const gchar *interface_name,
                          const gchar *signal_name,
                          GVariant *parameters,
                          gpointer data)
{
    CommonUserList *user_list = data;
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);

    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(o)")))
    {
        g_warning ("Got UserAccounts signal UserDeleted with unknown parameters %s", g_variant_get_type_string (parameters));
        return;
    }

    const gchar *path;
    g_variant_get (parameters, "(&o)", &path);

    /* Delete user if we know of them */
    CommonUser *user = get_user_by_path (user_list, path);
    if (user)
    {
        g_debug ("User %s deleted", path);
        priv->users = g_list_remove (priv->users, user);

        g_signal_emit (user_list, list_signals[USER_REMOVED], 0, user);

        g_object_unref (user);
    }
}

static CommonSession *
load_session (CommonUserList *user_list, const gchar *path)
{
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (priv->bus,
                                                              "org.freedesktop.DisplayManager",
                                                              path,
                                                              "org.freedesktop.DBus.Properties",
                                                              "Get",
                                                              g_variant_new ("(ss)", "org.freedesktop.DisplayManager.Session", "UserName"),
                                                              G_VARIANT_TYPE ("(v)"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Error getting UserName from org.freedesktop.DisplayManager.Session: %s", error->message);
    if (!result)
        return NULL;

    g_autoptr(GVariant) username = NULL;
    g_variant_get (result, "(v)", &username);
    if (!g_variant_is_of_type (username, G_VARIANT_TYPE_STRING))
        return NULL;

    const gchar *name;
    g_variant_get (username, "&s", &name);

    g_debug ("Loaded session %s (%s)", path, name);
    CommonSession *session = g_object_new (common_session_get_type (), NULL);
    session->username = g_strdup (name);
    session->path = g_strdup (path);
    priv->sessions = g_list_append (priv->sessions, session);

    return session;
}

static void
session_added_cb (GDBusConnection *connection,
                  const gchar *sender_name,
                  const gchar *object_path,
                  const gchar *interface_name,
                  const gchar *signal_name,
                  GVariant *parameters,
                  gpointer data)
{
    CommonUserList *user_list = data;

    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(o)")))
    {
        g_warning ("Got DisplayManager signal SessionAdded with unknown parameters %s", g_variant_get_type_string (parameters));
        return;
    }

    const gchar *path;
    g_variant_get (parameters, "(&o)", &path);
    CommonSession *session = load_session (user_list, path);
    if (!session)
        return;

    CommonUser *user = get_user_by_name (user_list, session->username);
    if (user)
        g_signal_emit (user, user_signals[CHANGED], 0);
}

static void
session_removed_cb (GDBusConnection *connection,
                    const gchar *sender_name,
                    const gchar *object_path,
                    const gchar *interface_name,
                    const gchar *signal_name,
                    GVariant *parameters,
                    gpointer data)
{
    CommonUserList *user_list = data;
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);

    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(o)")))
    {
        g_warning ("Got DisplayManager signal SessionRemoved with unknown parameters %s", g_variant_get_type_string (parameters));
        return;
    }

    const gchar *path;
    g_variant_get (parameters, "(&o)", &path);

    for (GList *link = priv->sessions; link; link = link->next)
    {
        CommonSession *session = link->data;
        if (strcmp (session->path, path) == 0)
        {
            g_debug ("Session %s removed", path);
            priv->sessions = g_list_delete_link (priv->sessions, link);
            CommonUser *user = get_user_by_name (user_list, session->username);
            if (user)
                g_signal_emit (user, user_signals[CHANGED], 0);
            g_object_unref (session);
            break;
        }
    }
}

static void
load_sessions (CommonUserList *user_list)
{
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);

    priv->session_added_signal = g_dbus_connection_signal_subscribe (priv->bus,
                                                                     "org.freedesktop.DisplayManager",
                                                                     "org.freedesktop.DisplayManager",
                                                                     "SessionAdded",
                                                                     "/org/freedesktop/DisplayManager",
                                                                     NULL,
                                                                     G_DBUS_SIGNAL_FLAGS_NONE,
                                                                     session_added_cb,
                                                                     user_list,
                                                                     NULL);
    priv->session_removed_signal = g_dbus_connection_signal_subscribe (priv->bus,
                                                                       "org.freedesktop.DisplayManager",
                                                                       "org.freedesktop.DisplayManager",
                                                                       "SessionRemoved",
                                                                       "/org/freedesktop/DisplayManager",
                                                                       NULL,
                                                                       G_DBUS_SIGNAL_FLAGS_NONE,
                                                                       session_removed_cb,
                                                                       user_list,
                                                                       NULL);

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (priv->bus,
                                                              "org.freedesktop.DisplayManager",
                                                              "/org/freedesktop/DisplayManager",
                                                              "org.freedesktop.DBus.Properties",
                                                              "Get",
                                                              g_variant_new ("(ss)", "org.freedesktop.DisplayManager", "Sessions"),
                                                              G_VARIANT_TYPE ("(v)"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Error getting session list from org.freedesktop.DisplayManager: %s", error->message);
    if (result)
    {
        if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(v)")))
        {
            g_autoptr(GVariant) value = NULL;
            g_variant_get (result, "(v)", &value);

            g_debug ("Loading sessions from org.freedesktop.DisplayManager");
            g_autoptr(GVariantIter) iter = NULL;
            g_variant_get (value, "ao", &iter);
            const gchar *path;
            while (g_variant_iter_loop (iter, "&o", &path))
                load_session (user_list, path);
        }
        else
            g_warning ("Unexpected type from org.freedesktop.DisplayManager.Sessions: %s", g_variant_get_type_string (result));
    }
}

static void
load_users (CommonUserList *user_list)
{
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);

    if (priv->have_users)
        return;
    priv->have_users = TRUE;

    /* Get user list from accounts service and fall back to /etc/passwd if that fails */
    priv->user_added_signal = g_dbus_connection_signal_subscribe (priv->bus,
                                                                  "org.freedesktop.Accounts",
                                                                  "org.freedesktop.Accounts",
                                                                  "UserAdded",
                                                                  "/org/freedesktop/Accounts",
                                                                  NULL,
                                                                  G_DBUS_SIGNAL_FLAGS_NONE,
                                                                  accounts_user_added_cb,
                                                                  user_list,
                                                                  NULL);
    priv->user_removed_signal = g_dbus_connection_signal_subscribe (priv->bus,
                                                                    "org.freedesktop.Accounts",
                                                                    "org.freedesktop.Accounts",
                                                                    "UserDeleted",
                                                                    "/org/freedesktop/Accounts",
                                                                    NULL,
                                                                    G_DBUS_SIGNAL_FLAGS_NONE,
                                                                    accounts_user_deleted_cb,
                                                                    user_list,
                                                                    NULL);

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (priv->bus,
                                                              "org.freedesktop.Accounts",
                                                              "/org/freedesktop/Accounts",
                                                              "org.freedesktop.Accounts",
                                                              "ListCachedUsers",
                                                              g_variant_new ("()"),
                                                              G_VARIANT_TYPE ("(ao)"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Error getting user list from org.freedesktop.Accounts: %s", error->message);
    if (result)
    {
        g_debug ("Loading users from org.freedesktop.Accounts");
        g_autoptr(GVariantIter) iter = NULL;
        g_variant_get (result, "(ao)", &iter);
        const gchar *path;
        while (g_variant_iter_loop (iter, "&o", &path))
            add_accounts_user (user_list, path, FALSE);
    }
    else
    {
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->user_added_signal);
        priv->user_added_signal = 0;
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->user_removed_signal);
        priv->user_removed_signal = 0;

        load_passwd_file (user_list, FALSE);

        /* Watch for changes to user list */
        g_autoptr(GFile) passwd_file = g_file_new_for_path (PASSWD_FILE);
        g_autoptr(GError) e = NULL;
        priv->passwd_monitor = g_file_monitor (passwd_file, G_FILE_MONITOR_NONE, NULL, &e);
        if (e)
            g_warning ("Error monitoring %s: %s", PASSWD_FILE, e->message);
        else
            g_signal_connect (priv->passwd_monitor, "changed", G_CALLBACK (passwd_changed_cb), user_list);
    }
}

/**
 * common_user_list_get_length:
 * @user_list: a #CommonUserList
 *
 * Return value: The number of users able to log in
 **/
gint
common_user_list_get_length (CommonUserList *user_list)
{
    g_return_val_if_fail (COMMON_IS_USER_LIST (user_list), 0);

    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);
    load_users (user_list);
    return g_list_length (priv->users);
}

/**
 * common_user_list_get_users:
 * @user_list: A #CommonUserList
 *
 * Get a list of users to present to the user.  This list may be a subset of the
 * available users and may be empty depending on the server configuration.
 *
 * Return value: (element-type CommonUser) (transfer none): A list of #CommonUser that should be presented to the user.
 **/
GList *
common_user_list_get_users (CommonUserList *user_list)
{
    g_return_val_if_fail (COMMON_IS_USER_LIST (user_list), NULL);

    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);
    load_users (user_list);
    return priv->users;
}

/**
 * common_user_list_get_user_by_name:
 * @user_list: A #CommonUserList
 * @username: Name of user to get.
 *
 * Get information about a given user or #NULL if this user doesn't exist.
 * Includes hidden and system users, unlike the list from
 * common_user_list_get_users.
 *
 * Return value: (transfer full): A #CommonUser entry for the given user.
 **/
CommonUser *
common_user_list_get_user_by_name (CommonUserList *user_list, const gchar *username)
{
    g_return_val_if_fail (COMMON_IS_USER_LIST (user_list), NULL);
    g_return_val_if_fail (username != NULL, NULL);

    load_users (user_list);

    CommonUser *user = get_user_by_name (user_list, username);
    if (user)
        return g_object_ref (user);

    /* Sometimes we need to look up users that aren't in AccountsService.
       Notably we need to look up the user that the greeter runs as, which
       is usually 'lightdm'. For such cases, we manually create a one-off
       CommonUser object and pre-seed with passwd info. */
    struct passwd *entry = getpwnam (username);
    if (entry != NULL)
        return make_passwd_user (user_list, entry);

    return NULL;
}

static void
common_user_list_init (CommonUserList *user_list)
{
    CommonUserListPrivate *priv = common_user_list_get_instance_private (user_list);

    priv->bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
}

static void
common_user_list_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
common_user_list_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    CommonUserList *self = COMMON_USER_LIST (object);

    switch (prop_id)
    {
    case LIST_PROP_NUM_USERS:
        g_value_set_int (value, common_user_list_get_length (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
common_user_list_finalize (GObject *object)
{
    CommonUserList *self = COMMON_USER_LIST (object);
    CommonUserListPrivate *priv = common_user_list_get_instance_private (self);

    /* Remove children first, they might access us */
    g_list_free_full (priv->users, g_object_unref);
    g_list_free_full (priv->sessions, g_object_unref);

    if (priv->user_added_signal)
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->user_added_signal);
    if (priv->user_removed_signal)
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->user_removed_signal);
    if (priv->session_added_signal)
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->session_added_signal);
    if (priv->session_removed_signal)
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->session_removed_signal);
    g_object_unref (priv->bus);
    g_clear_object (&priv->passwd_monitor);

    G_OBJECT_CLASS (common_user_list_parent_class)->finalize (object);
}

static void
common_user_list_class_init (CommonUserListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = common_user_list_set_property;
    object_class->get_property = common_user_list_get_property;
    object_class->finalize = common_user_list_finalize;

    g_object_class_install_property (object_class,
                                     LIST_PROP_NUM_USERS,
                                     g_param_spec_int ("num-users",
                                                       "num-users",
                                                       "Number of login users",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READABLE));
    /**
     * CommonUserList::user-added:
     * @user_list: A #CommonUserList
     * @user: The #CommonUser that has been added.
     *
     * The ::user-added signal gets emitted when a user account is created.
     **/
    list_signals[USER_ADDED] =
        g_signal_new (USER_LIST_SIGNAL_USER_ADDED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (CommonUserListClass, user_added),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, COMMON_TYPE_USER);

    /**
     * CommonUserList::user-changed:
     * @user_list: A #CommonUserList
     * @user: The #CommonUser that has been changed.
     *
     * The ::user-changed signal gets emitted when a user account is modified.
     **/
    list_signals[USER_CHANGED] =
        g_signal_new (USER_LIST_SIGNAL_USER_CHANGED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (CommonUserListClass, user_changed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, COMMON_TYPE_USER);

    /**
     * CommonUserList::user-removed:
     * @user_list: A #CommonUserList
     * @user: The #CommonUser that has been removed.
     *
     * The ::user-removed signal gets emitted when a user account is removed.
     **/
    list_signals[USER_REMOVED] =
        g_signal_new (USER_LIST_SIGNAL_USER_REMOVED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (CommonUserListClass, user_removed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, COMMON_TYPE_USER);
}

static gboolean
call_method (CommonUser *user, const gchar *method, GVariant *args,
             const gchar *expected, GVariant **result)
{
    CommonUserPrivate *priv = common_user_get_instance_private (user);

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) answer = g_dbus_connection_call_sync (priv->bus,
                                                              "org.freedesktop.Accounts",
                                                              priv->path,
                                                              "org.freedesktop.Accounts.User",
                                                              method,
                                                              args,
                                                              G_VARIANT_TYPE (expected),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              NULL,
                                                              &error);
    if (error)
        g_warning ("Could not call %s: %s", method, error->message);

    if (!answer)
        return FALSE;

    if (result)
        *result = g_steal_pointer (&answer);

    return TRUE;
}

static void
save_string_to_dmrc (CommonUser *user, const gchar *group,
                     const gchar *key, const gchar *value)
{
    g_autoptr(GKeyFile) dmrc = dmrc_load (user);
    g_key_file_set_string (dmrc, group, key, value);
    dmrc_save (dmrc, user);
}

/* Loads language/layout/session info for user */
static void
load_dmrc (CommonUser *user)
{
    CommonUserPrivate *priv = common_user_get_instance_private (user);

    /* We're using Accounts service instead */
    if (priv->path)
        return;

    if (priv->loaded_dmrc)
        return;
    priv->loaded_dmrc = TRUE;
    g_autoptr(GKeyFile) dmrc = dmrc_load (user);

    // FIXME: Watch for changes

    /* The Language field contains the locale */
    g_free (priv->language);
    priv->language = g_key_file_get_string (dmrc, "Desktop", "Language", NULL);

    if (g_key_file_has_key (dmrc, "Desktop", "Layout", NULL))
    {
        g_strfreev (priv->layouts);
        priv->layouts = g_malloc (sizeof (gchar *) * 2);
        priv->layouts[0] = g_key_file_get_string (dmrc, "Desktop", "Layout", NULL);
        priv->layouts[1] = NULL;
    }

    g_free (priv->session);
    priv->session = g_key_file_get_string (dmrc, "Desktop", "Session", NULL);
}

/**
 * common_user_get_name:
 * @user: A #CommonUser
 *
 * Get the name of a user.
 *
 * Return value: The name of the given user
 **/
const gchar *
common_user_get_name (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    return priv->name;
}

/**
 * common_user_get_real_name:
 * @user: A #CommonUser
 *
 * Get the real name of a user.
 *
 * Return value: The real name of the given user
 **/
const gchar *
common_user_get_real_name (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);
    CommonUserPrivate *priv = common_user_get_instance_private (user);

    return priv->real_name;
}

/**
 * common_user_get_display_name:
 * @user: A #CommonUser
 *
 * Get the display name of a user.
 *
 * Return value: The display name of the given user
 **/
const gchar *
common_user_get_display_name (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    if (!priv->real_name || strcmp (priv->real_name, "") == 0)
        return priv->name;
    else
        return priv->real_name;
}

/**
 * common_user_get_home_directory:
 * @user: A #CommonUser
 *
 * Get the home directory for a user.
 *
 * Return value: The users home directory
 */
const gchar *
common_user_get_home_directory (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    return priv->home_directory;
}

/**
 * common_user_get_shell:
 * @user: A #CommonUser
 *
 * Get the shell for a user.
 *
 * Return value: The user's shell
 */
const gchar *
common_user_get_shell (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    return priv->shell;
}

/**
 * common_user_get_image:
 * @user: A #CommonUser
 *
 * Get the image URI for a user.
 *
 * Return value: The image URI for the given user or #NULL if no URI
 **/
const gchar *
common_user_get_image (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    return priv->image;
}

/**
 * common_user_get_background:
 * @user: A #CommonUser
 *
 * Get the background file path for a user.
 *
 * Return value: The background file path for the given user or #NULL if no path
 **/
const gchar *
common_user_get_background (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    return priv->background;
}

/**
 * common_user_get_language:
 * @user: A #CommonUser
 *
 * Get the language for a user.
 *
 * Return value: The language in the form of a local specification (e.g. "de_DE.UTF-8") for the given user or #NULL if using the system default locale.
 **/
const gchar *
common_user_get_language (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    load_dmrc (user);
    const gchar *language = priv->language;
    return (language && language[0] == 0) ? NULL : language; /* Treat "" as NULL */
}

/**
 * common_user_set_language:
 * @user: A #CommonUser
 * @language: The user's new language
 *
 * Set the language for a user.
 **/
void
common_user_set_language (CommonUser *user, const gchar *language)
{
    g_return_if_fail (COMMON_IS_USER (user));
    if (g_strcmp0 (common_user_get_language (user), language) != 0)
    {
        call_method (user, "SetLanguage", g_variant_new ("(s)", language), "()", NULL);
        save_string_to_dmrc (user, "Desktop", "Language", language);
    }
}

/**
 * common_user_get_layout:
 * @user: A #CommonUser
 *
 * Get the keyboard layout for a user.
 *
 * Return value: The keyboard layout for the given user or #NULL if using system defaults.  Copy the value if you want to use it long term.
 **/
const gchar *
common_user_get_layout (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    load_dmrc (user);
    return priv->layouts[0];
}

/**
 * common_user_get_layouts:
 * @user: A #CommonUser
 *
 * Get the configured keyboard layouts for a user.
 *
 * Return value: (transfer none): A NULL-terminated array of keyboard layouts for the given user.  Copy the values if you want to use them long term.
 **/
const gchar * const *
common_user_get_layouts (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    load_dmrc (user);
    return (const gchar * const *) priv->layouts;
}

/**
 * common_user_get_session:
 * @user: A #CommonUser
 *
 * Get the session for a user.
 *
 * Return value: The session for the given user or #NULL if using system defaults.
 **/
const gchar *
common_user_get_session (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), NULL);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    load_dmrc (user);
    const gchar *session = priv->session;
    return (session && session[0] == 0) ? NULL : session; /* Treat "" as NULL */
}

/**
 * common_user_set_session:
 * @user: A #CommonUser
 * @session: The user's new session
 *
 * Set the session for a user.
 **/
void
common_user_set_session (CommonUser *user, const gchar *session)
{
    g_return_if_fail (COMMON_IS_USER (user));
    if (g_strcmp0 (common_user_get_session (user), session) != 0)
    {
        call_method (user, "SetXSession", g_variant_new ("(s)", session), "()", NULL);
        save_string_to_dmrc (user, "Desktop", "Session", session);
    }
}

/**
 * common_user_get_logged_in:
 * @user: A #CommonUser
 *
 * Check if a user is logged in.
 *
 * Return value: #TRUE if the user is currently logged in.
 **/
gboolean
common_user_get_logged_in (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), FALSE);

    gboolean result;
    g_signal_emit (user, user_signals[GET_LOGGED_IN], 0, &result);

    return result;
}

/**
 * common_user_get_has_messages:
 * @user: A #CommonUser
 *
 * Check if a user has waiting messages.
 *
 * Return value: #TRUE if the user has waiting messages.
 **/
gboolean
common_user_get_has_messages (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), FALSE);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    return priv->has_messages;
}

/**
 * common_user_get_uid:
 * @user: A #CommonUser
 *
 * Get the uid of a user
 *
 * Return value: The user's uid
 **/
uid_t
common_user_get_uid (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), 0);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    return priv->uid;
}

/**
 * common_user_get_gid:
 * @user: A #CommonUser
 *
 * Get the gid of a user
 *
 * Return value: The user's gid
 **/
gid_t
common_user_get_gid (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), 0);
    /* gid is not actually stored in AccountsService, so if our user is from
       AccountsService, we have to look up manually in passwd.  gid won't
       change, so just look up the first time we're asked and never again. */
    CommonUserPrivate *priv = common_user_get_instance_private (user);
    if (priv->uid != 0 && priv->gid == 0)
    {
        struct passwd *entry = getpwuid (priv->uid);
        if (entry != NULL)
            priv->gid = entry->pw_gid;
    }
    return priv->gid;
}

/**
 * common_user_get_is_locked:
 * @user: A #CommonUser
 *
 * Check if a user is locked.
 *
 * Return value: %TRUE if the user is locked.
 **/
gboolean
common_user_get_is_locked (CommonUser *user)
{
    g_return_val_if_fail (COMMON_IS_USER (user), FALSE);

    CommonUserPrivate *priv = common_user_get_instance_private (user);
    return priv->is_locked;
}

static void
common_user_init (CommonUser *user)
{
    CommonUserPrivate *priv = common_user_get_instance_private (user);
    priv->layouts = g_malloc (sizeof (gchar *) * 1);
    priv->layouts[0] = NULL;
}

static void
common_user_set_property (GObject    *object,
                           guint       prop_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
common_user_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    CommonUser *self = COMMON_USER (object);

    switch (prop_id)
    {
    case USER_PROP_NAME:
        g_value_set_string (value, common_user_get_name (self));
        break;
    case USER_PROP_REAL_NAME:
        g_value_set_string (value, common_user_get_real_name (self));
        break;
    case USER_PROP_DISPLAY_NAME:
        g_value_set_string (value, common_user_get_display_name (self));
        break;
    case USER_PROP_HOME_DIRECTORY:
        g_value_set_string (value, common_user_get_home_directory (self));
        break;
    case USER_PROP_SHELL:
        g_value_set_string (value, common_user_get_shell (self));
        break;
    case USER_PROP_IMAGE:
        g_value_set_string (value, common_user_get_image (self));
        break;
    case USER_PROP_BACKGROUND:
        g_value_set_string (value, common_user_get_background (self));
        break;
    case USER_PROP_LANGUAGE:
        g_value_set_string (value, common_user_get_language (self));
        break;
    case USER_PROP_LAYOUT:
        g_value_set_string (value, common_user_get_layout (self));
        break;
    case USER_PROP_LAYOUTS:
        g_value_set_boxed (value, g_strdupv ((gchar **) common_user_get_layouts (self)));
        break;
    case USER_PROP_SESSION:
        g_value_set_string (value, common_user_get_session (self));
        break;
    case USER_PROP_LOGGED_IN:
        g_value_set_boolean (value, common_user_get_logged_in (self));
        break;
    case USER_PROP_HAS_MESSAGES:
        g_value_set_boolean (value, common_user_get_has_messages (self));
        break;
    case USER_PROP_UID:
        g_value_set_uint64 (value, common_user_get_uid (self));
        break;
    case USER_PROP_GID:
        g_value_set_uint64 (value, common_user_get_gid (self));
        break;
    case USER_PROP_IS_LOCKED:
        g_value_set_boolean (value, common_user_get_is_locked (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
common_user_finalize (GObject *object)
{
    CommonUser *self = COMMON_USER (object);
    CommonUserPrivate *priv = common_user_get_instance_private (self);

    g_clear_pointer (&priv->path, g_free);
    if (priv->changed_signal)
        g_dbus_connection_signal_unsubscribe (priv->bus, priv->changed_signal);
    g_clear_object (&priv->bus);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->real_name, g_free);
    g_clear_pointer (&priv->home_directory, g_free);
    g_clear_pointer (&priv->shell, g_free);
    g_clear_pointer (&priv->image, g_free);
    g_clear_pointer (&priv->background, g_free);
    g_clear_pointer (&priv->language, g_free);
    g_clear_pointer (&priv->layouts, g_strfreev);
    g_clear_pointer (&priv->session, g_free);
}

static void
common_user_class_init (CommonUserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = common_user_set_property;
    object_class->get_property = common_user_get_property;
    object_class->finalize = common_user_finalize;

    g_object_class_install_property (object_class,
                                     USER_PROP_NAME,
                                     g_param_spec_string ("name",
                                                          "name",
                                                          "Username",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_REAL_NAME,
                                     g_param_spec_string ("real-name",
                                                          "real-name",
                                                          "Users real name",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_DISPLAY_NAME,
                                     g_param_spec_string ("display-name",
                                                          "display-name",
                                                          "Users display name",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_HOME_DIRECTORY,
                                     g_param_spec_string ("home-directory",
                                                          "home-directory",
                                                          "Home directory",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_SHELL,
                                     g_param_spec_string ("shell",
                                                          "shell",
                                                          "Shell",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_IMAGE,
                                     g_param_spec_string ("image",
                                                          "image",
                                                          "Avatar image",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_BACKGROUND,
                                     g_param_spec_string ("background",
                                                          "background",
                                                          "User background",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LANGUAGE,
                                     g_param_spec_string ("language",
                                                         "language",
                                                         "Language used by this user",
                                                         NULL,
                                                         G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LAYOUT,
                                     g_param_spec_string ("layout",
                                                          "layout",
                                                          "Keyboard layout used by this user",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LAYOUTS,
                                     g_param_spec_boxed ("layouts",
                                                         "layouts",
                                                         "Keyboard layouts used by this user",
                                                         G_TYPE_STRV,
                                                         G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_SESSION,
                                     g_param_spec_string ("session",
                                                          "session",
                                                          "Session used by this user",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LOGGED_IN,
                                     g_param_spec_boolean ("logged-in",
                                                           "logged-in",
                                                           "TRUE if the user is currently in a session",
                                                           FALSE,
                                                           G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_LOGGED_IN,
                                     g_param_spec_boolean ("has-messages",
                                                           "has-messages",
                                                           "TRUE if the user is has waiting messages",
                                                           FALSE,
                                                           G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_UID,
                                     g_param_spec_uint64 ("uid",
                                                          "uid",
                                                          "Uid",
                                                          0,
                                                          G_MAXUINT64,
                                                          0,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_GID,
                                     g_param_spec_uint64 ("gd",
                                                          "gid",
                                                          "Gid",
                                                          0,
                                                          G_MAXUINT64,
                                                          0,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     USER_PROP_IS_LOCKED,
                                     g_param_spec_boolean ("is-locked",
                                                           "is-locked",
                                                           "TRUE if the user is currently locked",
                                                           FALSE,
                                                           G_PARAM_READABLE));
    /**
     * CommonUser::changed:
     * @user: A #CommonUser
     *
     * The ::changed signal gets emitted this user account is modified.
     **/
    user_signals[CHANGED] =
        g_signal_new (USER_SIGNAL_CHANGED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (CommonUserClass, changed),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    user_signals[GET_LOGGED_IN] =
        g_signal_new ("get-logged-in",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      g_signal_accumulator_first_wins,
                      NULL,
                      NULL,
                      G_TYPE_BOOLEAN, 0);
}

static void
common_session_init (CommonSession *common_session)
{
}

static void
common_session_finalize (GObject *object)
{
    CommonSession *self = COMMON_SESSION (object);

    g_clear_pointer (&self->path, g_free);
    g_clear_pointer (&self->username, g_free);
}

static void
common_session_class_init (CommonSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = common_session_finalize;
}
