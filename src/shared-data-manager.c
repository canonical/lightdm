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

struct SharedDataManagerPrivate
{
    gchar *greeter_user;
    guint32 greeter_gid;
    GHashTable *starting_dirs;
};

struct OwnerInfo
{
    SharedDataManager *manager;
    guint32 uid;
};

G_DEFINE_TYPE (SharedDataManager, shared_data_manager, G_TYPE_OBJECT);

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
    if (singleton)
    {
        g_object_unref (singleton);
        singleton = NULL;
    }
}

static void
delete_unused_user (gpointer key, gpointer value, gpointer user_data)
{
    const gchar *user = (const gchar *)key;
    GError *error = NULL;

    /* For this operation, we just need a fire and forget rm -rf.  Since
       recursively deleting in GIO is a huge pain in the butt, we'll just drop
       to shell for this. */

    gchar *path = g_build_filename (USERS_DIR, user, NULL);
    gchar *quoted_path = g_shell_quote (path);
    gchar *cmd = g_strdup_printf ("/bin/rm -rf %s", quoted_path);

    g_spawn_command_line_async (cmd, &error);
    if (error)
        g_warning ("Could not delete unused user data directory %s: %s", path, error->message);
    g_clear_error (&error);

    g_free (cmd);
    g_free (quoted_path);
    g_free (path);
}

gchar *
shared_data_manager_ensure_user_dir (SharedDataManager *manager, const gchar *user)
{
    struct passwd *entry;
    gchar *path;
    GFile *file;
    gboolean result;
    GFileInfo *info;
    GError *error = NULL;

    entry = getpwnam (user);
    if (!entry)
        return NULL;

    path = g_build_filename (USERS_DIR, user, NULL);
    file = g_file_new_for_path (path);

    g_debug ("Creating shared data directory %s", path);

    result = g_file_make_directory (file, NULL, &error);
    if (error)
    {
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
            result = TRUE;
        else
            g_warning ("Could not create user data directory %s: %s", path, error->message);
    }
    g_clear_error (&error);
    if (!result)
    {
        g_object_unref (file);
        g_free (path);
        return NULL;
    }

    /* Even if the directory already exists, we want to re-affirm the owners
       because the greeter gid is configuration based and may change between
       runs. */
    info = g_file_info_new ();
    g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID, entry->pw_uid);
    g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_GID, manager->priv->greeter_gid);
    g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE, 0770);
    result = g_file_set_attributes_from_info (file, info, G_FILE_QUERY_INFO_NONE, NULL, &error);
    g_object_unref (info);
    g_object_unref (file);
    if (error)
        g_warning ("Could not chown user data directory %s: %s", path, error->message);
    if (!result)
    {
        g_free (path);
        return NULL;
    }

    return path;
}

static void
next_user_dirs_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
    GFileEnumerator *enumerator = G_FILE_ENUMERATOR (object);
    SharedDataManager *manager = SHARED_DATA_MANAGER (user_data);
    GList *link;
    GError *error = NULL;

    GList *files = g_file_enumerator_next_files_finish (enumerator, res, &error);
    if (error)
    {
        g_warning ("Could not enumerate user data directory %s: %s", USERS_DIR, error->message);
        g_clear_error (&error);
        g_object_unref (manager);
        return;
    }

    for (link = files; link; link = link->next)
    {
        GFileInfo *info = link->data;
        g_hash_table_insert (manager->priv->starting_dirs, g_strdup (g_file_info_get_name (info)), NULL);
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
        for (link = users; link; link = link->next)
        {
            CommonUser *user = link->data;
            g_hash_table_remove (manager->priv->starting_dirs, common_user_get_name (user));
        }
        g_hash_table_foreach (manager->priv->starting_dirs, delete_unused_user, manager);
        g_hash_table_destroy (manager->priv->starting_dirs);
        manager->priv->starting_dirs = NULL;

        g_object_unref (manager);
    }
}

static void
list_user_dirs_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
    GFile *file = G_FILE (object);
    SharedDataManager *manager = SHARED_DATA_MANAGER (user_data);
    GFileEnumerator *enumerator;
    GError *error = NULL;

    enumerator = g_file_enumerate_children_finish (file, res, &error);
    if (error)
        g_warning ("Could not enumerate user data directory %s: %s", USERS_DIR, error->message);
    g_clear_error (&error);
    if (!enumerator)
    {
        g_object_unref (manager);
        return;
    }

    manager->priv->starting_dirs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_file_enumerator_next_files_async (enumerator, NUM_ENUMERATION_FILES,
                                        G_PRIORITY_DEFAULT, NULL,
                                        next_user_dirs_cb, manager);
}

static void
user_removed_cb (CommonUserList *list, CommonUser *user, SharedDataManager *manager)
{
    delete_unused_user ((gpointer) common_user_get_name (user), NULL, manager);
}

void
shared_data_manager_start (SharedDataManager *manager)
{
    GFile *file;

    /* Grab list of all current directories, so we know if any exist that we no longer need. */
    file = g_file_new_for_path (USERS_DIR);
    g_file_enumerate_children_async (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                     G_FILE_QUERY_INFO_NONE,
                                     G_PRIORITY_DEFAULT, NULL,
                                     list_user_dirs_cb, g_object_ref (manager));
    g_object_unref (file);

    /* And listen for user removals. */
    g_signal_connect (common_user_list_get_instance (), USER_LIST_SIGNAL_USER_REMOVED, G_CALLBACK (user_removed_cb), manager);
}

static void
shared_data_manager_init (SharedDataManager *manager)
{
    struct passwd *greeter_entry;

    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, SHARED_DATA_MANAGER_TYPE, SharedDataManagerPrivate);

    /* Grab current greeter-user gid */
    manager->priv->greeter_user = config_get_string (config_get_instance (), "LightDM", "greeter-user");
    greeter_entry = getpwnam (manager->priv->greeter_user);
    if (greeter_entry)
        manager->priv->greeter_gid = greeter_entry->pw_gid;
}

static void
shared_data_manager_dispose (GObject *object)
{
    SharedDataManager *self = SHARED_DATA_MANAGER (object);

    /* Should also cancel outstanding GIO operations, but whatever, let them do their thing. */

    g_signal_handlers_disconnect_by_data (common_user_list_get_instance (), self);

    G_OBJECT_CLASS (shared_data_manager_parent_class)->dispose (object);
}

static void
shared_data_manager_finalize (GObject *object)
{
    SharedDataManager *self = SHARED_DATA_MANAGER (object);

    if (self->priv->starting_dirs)
        g_hash_table_destroy (self->priv->starting_dirs);

    g_free (self->priv->greeter_user);

    G_OBJECT_CLASS (shared_data_manager_parent_class)->finalize (object);
}

static void
shared_data_manager_class_init (SharedDataManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = shared_data_manager_dispose;
    object_class->finalize = shared_data_manager_finalize;

    g_type_class_add_private (klass, sizeof (SharedDataManagerPrivate));
}
