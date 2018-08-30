/*
 * Copyright (C) 2014 Canonical, Ltd
 * Author: Michael Terry <michael.terry@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <config.h>
#include <gio/gio.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include "configuration.h"
#include "shared-data-manager.h"
#include "user-list.h"

#define NUM_ENUMERATION_FILES 100

typedef struct
{
    gchar *greeter_user;
    guint32 greeter_gid;
    GHashTable *starting_dirs;
} SharedDataManagerPrivate;

struct OwnerInfo
{
    SharedDataManager *manager;
    guint32 uid;
};

G_DEFINE_TYPE_WITH_PRIVATE (SharedDataManager, shared_data_manager, G_TYPE_OBJECT)

static SharedDataManager *singleton = NULL;

SharedDataManager *
shared_data_manager_get_instance (void)
{
    if (!singleton)
        singleton = g_object_new (SHARED_DATA_MANAGER_TYPE, NULL);
    return singleton;
}

void
shared_data_manager_cleanup (void)
{
    g_clear_object (&singleton);
}

static void
delete_unused_user (gpointer key, gpointer value, gpointer user_data)
{
    const gchar *user = (const gchar *)key;

    /* For this operation, we just need a fire and forget rm -rf.  Since
       recursively deleting in GIO is a huge pain in the butt, we'll just drop
       to shell for this. */

    g_autofree gchar *path = g_build_filename (USERS_DIR, user, NULL);
    g_autofree gchar *quoted_path = g_shell_quote (path);
    g_autofree gchar *cmd = g_strdup_printf ("/bin/rm -rf %s", quoted_path);

    g_autoptr(GError) error = NULL;
    g_spawn_command_line_async (cmd, &error);
    if (error)
        g_warning ("Could not delete unused user data directory %s: %s", path, error->message);
}

gchar *
shared_data_manager_ensure_user_dir (SharedDataManager *manager, const gchar *user)
{
    SharedDataManagerPrivate *priv = shared_data_manager_get_instance_private (manager);

    struct passwd *entry = getpwnam (user);
    if (!entry)
        return NULL;

    g_autofree gchar *path = g_build_filename (USERS_DIR, user, NULL);
    g_autoptr(GFile) file = g_file_new_for_path (path);

    g_debug ("Creating shared data directory %s", path);

    g_autoptr(GError) error = NULL;
    gboolean result = g_file_make_directory (file, NULL, &error);
    if (error)
    {
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        {
            g_clear_error (&error);
            result = TRUE;
        }
        else
            g_warning ("Could not create user data directory %s: %s", path, error->message);
    }
    if (!result)
    {
        g_clear_pointer (&path, g_free);
        return NULL;
    }

    /* Even if the directory already exists, we want to re-affirm the owners
       because the greeter gid is configuration based and may change between
       runs. */
    g_autoptr(GFileInfo) info = g_file_info_new ();
    g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID, entry->pw_uid);
    g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_GID, priv->greeter_gid);
    g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE, 0770);
    result = g_file_set_attributes_from_info (file, info, G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (error)
        g_warning ("Could not chown user data directory %s: %s", path, error->message);
    if (!result)
        return NULL;

    return g_steal_pointer (&path);
}

static void
next_user_dirs_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
    GFileEnumerator *enumerator = G_FILE_ENUMERATOR (object);
    SharedDataManager *manager = SHARED_DATA_MANAGER (user_data);
    SharedDataManagerPrivate *priv = shared_data_manager_get_instance_private (manager);

    g_autoptr(GError) error = NULL;
    GList *files = g_file_enumerator_next_files_finish (enumerator, res, &error);
    if (error)
    {
        g_warning ("Could not enumerate user data directory %s: %s", USERS_DIR, error->message);
        g_object_unref (manager);
        return;
    }

    for (GList *link = files; link; link = link->next)
    {
        GFileInfo *info = link->data;
        g_hash_table_insert (priv->starting_dirs, g_strdup (g_file_info_get_name (info)), NULL);
    }

    if (files != NULL)
    {
        g_list_free_full (files, g_object_unref);
        g_file_enumerator_next_files_async (enumerator, NUM_ENUMERATION_FILES, G_PRIORITY_DEFAULT, NULL, next_user_dirs_cb, manager);
    }
    else
    {
        // We've finally assembled all the initial directories.  Now let's
        // iterate the current users and as we go, remove the users from the
        // starting_dirs hash and thus see which users are obsolete.
        GList *users = common_user_list_get_users (common_user_list_get_instance ());
        for (GList *link = users; link; link = link->next)
        {
            CommonUser *user = link->data;
            g_hash_table_remove (priv->starting_dirs, common_user_get_name (user));
        }
        g_hash_table_foreach (priv->starting_dirs, delete_unused_user, manager);
        g_hash_table_destroy (priv->starting_dirs);
        priv->starting_dirs = NULL;

        g_object_unref (manager);
    }
}

static void
list_user_dirs_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
    GFile *file = G_FILE (object);
    g_autoptr(SharedDataManager) manager = SHARED_DATA_MANAGER (user_data);
    SharedDataManagerPrivate *priv = shared_data_manager_get_instance_private (manager);

    g_autoptr(GError) error = NULL;
    GFileEnumerator *enumerator = g_file_enumerate_children_finish (file, res, &error);
    if (error)
        g_warning ("Could not enumerate user data directory %s: %s", USERS_DIR, error->message);
    if (!enumerator)
        return;

    priv->starting_dirs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_file_enumerator_next_files_async (enumerator, NUM_ENUMERATION_FILES,
                                        G_PRIORITY_DEFAULT, NULL,
                                        next_user_dirs_cb, g_steal_pointer (&manager));
}

static void
user_removed_cb (CommonUserList *list, CommonUser *user, SharedDataManager *manager)
{
    delete_unused_user ((gpointer) common_user_get_name (user), NULL, manager);
}

void
shared_data_manager_start (SharedDataManager *manager)
{
    /* Grab list of all current directories, so we know if any exist that we no longer need. */
    g_autoptr(GFile) file = g_file_new_for_path (USERS_DIR);
    g_file_enumerate_children_async (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                     G_FILE_QUERY_INFO_NONE,
                                     G_PRIORITY_DEFAULT, NULL,
                                     list_user_dirs_cb, g_object_ref (manager));

    /* And listen for user removals. */
    g_signal_connect (common_user_list_get_instance (), USER_LIST_SIGNAL_USER_REMOVED, G_CALLBACK (user_removed_cb), manager);
}

static void
shared_data_manager_init (SharedDataManager *manager)
{
    SharedDataManagerPrivate *priv = shared_data_manager_get_instance_private (manager);

    /* Grab current greeter-user gid */
    priv->greeter_user = config_get_string (config_get_instance (), "LightDM", "greeter-user");
    struct passwd *greeter_entry = getpwnam (priv->greeter_user);
    if (greeter_entry)
        priv->greeter_gid = greeter_entry->pw_gid;
}

static void
shared_data_manager_finalize (GObject *object)
{
    SharedDataManager *self = SHARED_DATA_MANAGER (object);
    SharedDataManagerPrivate *priv = shared_data_manager_get_instance_private (self);

    /* Should also cancel outstanding GIO operations, but whatever, let them do their thing. */

    g_signal_handlers_disconnect_by_data (common_user_list_get_instance (), self);

    if (priv->starting_dirs)
        g_hash_table_destroy (priv->starting_dirs);

    g_clear_pointer (&priv->greeter_user, g_free);

    G_OBJECT_CLASS (shared_data_manager_parent_class)->finalize (object);
}

static void
shared_data_manager_class_init (SharedDataManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = shared_data_manager_finalize;
}
