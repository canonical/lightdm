/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include "session-manager.h"
#include "session-manager-glue.h"

struct SessionManagerPrivate
{
    gboolean sessions_loaded;
    GList *sessions;
};

G_DEFINE_TYPE (SessionManager, session_manager, G_TYPE_OBJECT);

SessionManager *
session_manager_new (void)
{
    return g_object_new (SESSION_MANAGER_TYPE, NULL);
}

static void
session_free (Session *session)
{
    g_free (session->name);
    g_free (session->comment);
    g_free (session->exec);
    g_free (session);
}

static Session *
load_session (GKeyFile *key_file, GError **error)
{
    Session *session;
  
    session = g_malloc0 (sizeof (Session));

    session->name = g_key_file_get_locale_string(key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, NULL, error);
    if (!session->name)
    {
        session_free (session);
        return NULL;
    }

    session->comment = g_key_file_get_locale_string(key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_COMMENT, NULL, error);
    if (!session->comment)
    {
        session_free (session);
        return NULL;
    }

    session->exec = g_key_file_get_value(key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, error);
    if (!session->exec)
    {
        session_free (session);
        return NULL;
    }

    return session;
}

static void
load_sessions (SessionManager *manager)
{
    GDir *directory;
    GError *error = NULL;
    GKeyFile *key_file;

    if (manager->priv->sessions_loaded)
        return;

    directory = g_dir_open (XSESSIONS_DIR, 0, &error);
    if (!directory)
        g_warning ("Failed to open sessions directory: %s", error->message);
    g_clear_error (&error);
    if (!directory)
        return;

    key_file = g_key_file_new ();
    while (TRUE)
    {
        const gchar *filename;
        gchar *path;
        gboolean result;
        Session *session;

        filename = g_dir_read_name (directory);
        if (filename == NULL)
            break;

        if (!g_str_has_suffix (filename, ".desktop"))
            continue;

        path = g_build_filename (XSESSIONS_DIR, filename, NULL);
        g_debug ("Loading session %s", path);

        result = g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error);
        if (!result)
            g_warning ("Failed to load session file %s: %s:", path, error->message);
        g_clear_error (&error);

        if (result)
        {
            session = load_session (key_file, &error);
            if (session)
            {
                g_debug ("Loaded session %s (%s)", session->name, session->comment);
                manager->priv->sessions = g_list_append (manager->priv->sessions, session);
            }
            else
                g_warning ("Invalid session %s: %s", path, error->message);
            g_clear_error (&error);
        }

        g_free (path);
    }

    g_dir_close (directory);
    g_key_file_free (key_file);

    manager->priv->sessions_loaded = TRUE;
}

#define TYPE_SESSION dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID)

gboolean
session_manager_get_sessions (SessionManager *manager, GPtrArray **sessions, GError *error)
{
    GList *link;

    load_sessions (manager);

    *sessions = g_ptr_array_sized_new (g_list_length (manager->priv->sessions));
    for (link = manager->priv->sessions; link; link = link->next)
    {
        Session *session = link->data;
        GValue value = { 0 };

        g_value_init (&value, TYPE_SESSION);
        g_value_take_boxed (&value, dbus_g_type_specialized_construct (TYPE_SESSION));
        dbus_g_type_struct_set (&value, 0, session->name, 1, session->comment, G_MAXUINT);
        g_ptr_array_add (*sessions, g_value_get_boxed (&value));
    }

    return TRUE;
}

static void
session_manager_init (SessionManager *manager)
{
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, SESSION_MANAGER_TYPE, SessionManagerPrivate);
}

static void
session_manager_class_init (SessionManagerClass *klass)
{
    g_type_class_add_private (klass, sizeof (SessionManagerPrivate));

    dbus_g_object_type_install_info (SESSION_MANAGER_TYPE, &dbus_glib_session_manager_object_info);
}
