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

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dbus/dbus-glib.h>
#include <xcb/xcb.h>
#include <pwd.h>

#include "display-manager.h"
#include "display-manager-glue.h"
#include "xdmcp-server.h"
#include "xserver.h"
#include "theme.h"

enum {
    DISPLAY_ADDED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct DisplayManagerPrivate
{
    /* Configuration */
    GKeyFile *config;

    /* Directory to store authorization files */
    gchar *auth_dir;

    /* Counter to generate unique authorization file names */
    guint auth_counter;

    /* Directory to write log files to */
    gchar *log_dir;

    /* TRUE if running in test mode (i.e. as non-root for testing) */
    gboolean test_mode;

    /* The displays being managed */
    GList *displays;

    /* XDMCP server */
    XDMCPServer *xdmcp_server;
};

G_DEFINE_TYPE (DisplayManager, display_manager, G_TYPE_OBJECT);

DisplayManager *
display_manager_new (GKeyFile *config)
{
    DisplayManager *self = g_object_new (DISPLAY_MANAGER_TYPE, NULL);

    self->priv->config = config;
    self->priv->test_mode = g_key_file_get_boolean (self->priv->config, "LightDM", "test-mode", NULL);
    self->priv->auth_dir = g_key_file_get_string (self->priv->config, "LightDM", "authorization-directory", NULL);
    if (!self->priv->auth_dir)
        self->priv->auth_dir = g_strdup (XAUTH_DIR);
    self->priv->log_dir = g_key_file_get_string (self->priv->config, "LightDM", "log-directory", NULL);
    if (!self->priv->log_dir)
        self->priv->log_dir = g_strdup (LOG_DIR);

    return self;
}

static gboolean
display_number_used (DisplayManager *manager, guint display_number)
{
    GList *link;

    for (link = manager->priv->displays; link; link = link->next)
    {
        Display *display = link->data;      
        XServer *xserver = display_get_xserver (display);
        if (xserver && xserver_get_hostname (xserver) == NULL && xserver_get_display_number (xserver) == display_number)
            return TRUE;
    }

    /* In test mode there is probably another display manager running so see if the server exists */
    if (manager->priv->test_mode)
    {
        xcb_connection_t *connection;
        gchar *address;
        gboolean is_used;

        address = g_strdup_printf (":%d", display_number);
        connection = xcb_connect_to_display_with_auth_info (address, NULL, NULL);
        g_free (address);
        is_used = !xcb_connection_has_error (connection);
        xcb_disconnect (connection);

        return is_used;
    }

    return FALSE;
}

static guint
get_free_display_number (DisplayManager *manager)
{
    guint display_number = 0;

    while (display_number_used (manager, display_number))
        display_number++;
  
    return display_number;
}

static gchar *
get_authorization_path (DisplayManager *manager)
{
    gchar *path;

    path = g_strdup_printf ("%s/%d", manager->priv->auth_dir, manager->priv->auth_counter);
    manager->priv->auth_counter++;

    return path;
}

static void
start_session (Display *display, Session *session, gboolean is_greeter, DisplayManager *manager)
{
    gchar *string;
    XAuthorization *authorization;

    /* Connect using the session bus */
    if (manager->priv->test_mode)
    {
        session_set_env (session, "DBUS_SESSION_BUS_ADDRESS", getenv ("DBUS_SESSION_BUS_ADDRESS"));
        session_set_env (session, "XDG_SESSION_COOKIE", getenv ("XDG_SESSION_COOKIE"));
        session_set_env (session, "LDM_BUS", "SESSION");
    }

    /* Address for greeter to connect to */
    string = g_strdup_printf ("/org/lightdm/LightDisplayManager/Display%d", display_get_index (display));
    session_set_env (session, "LDM_DISPLAY", string);
    g_free (string);

    authorization = xserver_get_authorization (display_get_xserver (display));
    if (authorization)
    {
        gchar *path;

        path = get_authorization_path (manager);
        session_set_authorization (session, authorization, path);
        g_free (path);
    }

    if (is_greeter)
    {
        gchar *filename;
        filename = g_strdup_printf ("%s-greeter.log", xserver_get_address (display_get_xserver (display)));
        string = g_build_filename (manager->priv->log_dir, filename, NULL);
        g_free (filename);
    }
    else
    {
        // FIXME: Copy old error file
        if (manager->priv->test_mode)
            string = g_strdup (".xsession-errors");
        else
        {
            struct passwd *user_info = getpwnam (session_get_username (session));
            if (user_info)
                string = g_build_filename (user_info->pw_dir, ".xsession-errors", NULL);
            else
                g_warning ("Failed to get user info for user '%s'", session_get_username (session));
        }
    }
    g_debug ("Logging to %s", string);
    session_set_log_file (session, string);
    g_free (string);
}

static void
start_greeter_cb (Display *display, Session *session, DisplayManager *manager)
{
    start_session (display, session, TRUE, manager);
}

static void
start_session_cb (Display *display, Session *session, DisplayManager *manager)
{
    start_session (display, session, FALSE, manager);
}

static void
end_session_cb (Display *display, Session *session, DisplayManager *manager)
{
    XServer *xserver;

    /* Change authorization for next session */
    xserver = display_get_xserver (display);
    if (xserver_get_server_type (xserver) == XSERVER_TYPE_LOCAL)
    {
        XAuthorization *authorization;

        g_debug ("Generating new authorization cookie for %s", xserver_get_address (xserver));
        authorization = xauth_new_cookie ();
        xserver_set_authorization (xserver, authorization, NULL);
        g_object_unref (authorization);
    }
}

static guchar
atox (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

static void
string_to_xdm_auth_key (const gchar *key, guchar *data)
{
    gint i;

    memset (data, 0, sizeof (data));
    if (strncmp (key, "0x", 2) == 0 || strncmp (key, "0X", 2) == 0)
    {
        for (i = 0; i < 8; i++)
        {
            if (key[i*2] == '\0')
                break;
            data[i] |= atox (key[i*2]) << 8;
            if (key[i*2+1] == '\0')
                break;
            data[i] |= atox (key[i*2+1]);
        }
    }
    else
    {
        for (i = 1; i < 8 && key[i-1]; i++)
           data[i] = key[i-1];
    }
}

static XServer *
make_xserver (DisplayManager *manager, gchar *config_section)
{
    gint display_number;
    XServer *xserver;
    XAuthorization *authorization = NULL;
    gchar *xdmcp_manager, *filename, *path, *xserver_command;

    if (config_section && g_key_file_has_key (manager->priv->config, config_section, "display-number", NULL))
        display_number = g_key_file_get_integer (manager->priv->config, config_section, "display-number", NULL);
    else
        display_number = get_free_display_number (manager);

    xdmcp_manager = config_section ? g_key_file_get_string (manager->priv->config, config_section, "xdmcp-manager", NULL) : NULL;
    if (xdmcp_manager)
    {
        gint port;
        gchar *key;

        xserver = xserver_new (XSERVER_TYPE_LOCAL_TERMINAL, xdmcp_manager, display_number);

        port = g_key_file_get_integer (manager->priv->config, config_section, "xdmcp-port", NULL);
        if (port > 0)
            xserver_set_port (xserver, port);
        key = g_key_file_get_string (manager->priv->config, config_section, "key", NULL);
        if (key)
        {
            guchar data[8];

            string_to_xdm_auth_key (key, data);
            xserver_set_authentication (xserver, "XDM-AUTHENTICATION-1", data, 8);
            authorization = xauth_new ("XDM-AUTHORIZATION-1", data, 8);
        }
    }
    else
    {
        xserver = xserver_new (XSERVER_TYPE_LOCAL, NULL, display_number);
        authorization = xauth_new_cookie ();
    }
    g_free (xdmcp_manager);

    path = get_authorization_path (manager);
    xserver_set_authorization (xserver, authorization, path);
    g_object_unref (authorization);
    g_free (path);

    filename = g_strdup_printf ("%s.log", xserver_get_address (xserver));
    path = g_build_filename (manager->priv->log_dir, filename, NULL);
    g_debug ("Logging to %s", path);
    xserver_set_log_file (xserver, path);
    g_free (filename);
    g_free (path);

    /* Allow X server to be Xephyr */
    if (getenv ("DISPLAY"))
        xserver_set_env (xserver, "DISPLAY", getenv ("DISPLAY"));
    if (getenv ("XAUTHORITY"))
        xserver_set_env (xserver, "XAUTHORITY", getenv ("XAUTHORITY"));
    xserver_command = g_key_file_get_string (manager->priv->config, "LightDM", "xserver", NULL);
    if (xserver_command)
        xserver_set_command (xserver, xserver_command);
    g_free (xserver_command);

    if (manager->priv->test_mode)
        xserver_set_command (xserver, "Xephyr");
  
    return xserver;
}

static Display *
add_display (DisplayManager *manager)
{
    Display *display;
    gchar *value;

    display = display_new (g_list_length (manager->priv->displays));
    g_signal_connect (display, "start-greeter", G_CALLBACK (start_greeter_cb), manager);
    g_signal_connect (display, "start-session", G_CALLBACK (start_session_cb), manager);
    g_signal_connect (display, "end-session", G_CALLBACK (end_session_cb), manager);

    if (manager->priv->test_mode)
        display_set_greeter_user (display, NULL);

    value = g_key_file_get_string (manager->priv->config, "Greeter", "language", NULL);
    if (value)
        display_set_default_language (display, value);
    g_free (value);
    value = g_key_file_get_string (manager->priv->config, "Greeter", "layout", NULL);
    if (value)
        display_set_default_layout (display, value);
    g_free (value);
    value = g_key_file_get_string (manager->priv->config, "Greeter", "session", NULL);
    if (value)
        display_set_default_session (display, value);
    g_free (value);
    value = g_key_file_get_string (manager->priv->config, "Greeter", "user", NULL);
    if (value)
        display_set_greeter_user (display, value);
    g_free (value);
    value = g_key_file_get_string (manager->priv->config, "Greeter", "theme", NULL);
    if (value)
        display_set_greeter_theme (display, value);
    g_free (value);

    manager->priv->displays = g_list_append (manager->priv->displays, display);

    g_signal_emit (manager, signals[DISPLAY_ADDED], 0, display);

    return display;
}

gboolean
display_manager_add_display (DisplayManager *manager, GError *error)
{
    Display *display;
    XServer *xserver;

    g_debug ("Starting new display");
    display = add_display (manager);
    xserver = make_xserver (manager, NULL);
    display_set_xserver (display, xserver);
    display_start (display);
    g_object_unref (xserver);

    return TRUE;
}

gboolean
display_manager_switch_to_user (DisplayManager *manager, char *username, GError *error)
{
    GList *link;
    Display *display;
    XServer *xserver;

    for (link = manager->priv->displays; link; link = link->next)
    {
        display = link->data;
        const gchar *session_user;

        session_user = display_get_session_user (display);
        if (session_user && strcmp (session_user, username) == 0)
        {
            g_debug ("Switching to user %s session on display %s", username, xserver_get_address (display_get_xserver (display)));
            //display_focus (display);
            return TRUE;
        }
    }

    g_debug ("Starting new display for user %s", username);
    display = add_display (manager);
    xserver = make_xserver (manager, NULL);
    display_set_xserver (display, xserver);
    display_start (display);
    g_object_unref (xserver);

    return TRUE;
}

static gboolean
xdmcp_session_cb (XDMCPServer *server, XDMCPSession *session, DisplayManager *manager)
{
    Display *display;
    gchar *address;
    XServer *xserver;
    gboolean result;
  
    // FIXME: Try IPv6 then fallback to IPv4

    display = add_display (manager);
    address = g_inet_address_to_string (G_INET_ADDRESS (xdmcp_session_get_address (session)));
    xserver = xserver_new (XSERVER_TYPE_REMOTE, address, xdmcp_session_get_display_number (session));
    if (strcmp (xdmcp_session_get_authorization_name (session), "") != 0)
    {
        XAuthorization *authorization = NULL;
        gchar *path;

        authorization = xauth_new (xdmcp_session_get_authorization_name (session),
                                   xdmcp_session_get_authorization_data (session),
                                   xdmcp_session_get_authorization_data_length (session));
        path = get_authorization_path (manager);

        xserver_set_authorization (xserver, authorization, path);

        g_object_unref (authorization);
        g_free (path);
    }

    display_set_xserver (display, xserver);
    result = display_start (display);
    g_object_unref (xserver);
    g_free (address);
    if (!result)
       g_object_unref (display);

    return result;
}

static void
setup_auth_dir (DisplayManager *manager)
{
    GDir *dir;
    GError *error = NULL;

    g_mkdir_with_parents (manager->priv->auth_dir, S_IRWXU | S_IXGRP | S_IXOTH);
    dir = g_dir_open (manager->priv->auth_dir, 0, &error);
    if (!dir)
    {
        g_warning ("Authorization dir not created: %s", error->message);
        g_clear_error (&error);
        return;
    }

    /* Clear out the directory */
    while (TRUE)
    {
        const gchar *filename;
        gchar *path;
        GFile *file;

        filename = g_dir_read_name (dir);
        if (!filename)
            break;

        path = g_build_filename (manager->priv->auth_dir, filename, NULL);
        file = g_file_new_for_path (filename);
        g_file_delete (file, NULL, NULL);

        g_free (path);
        g_object_unref (file);
    }

    g_dir_close (dir);
}

void
display_manager_start (DisplayManager *manager)
{
    gchar *displays;
    gchar **tokens, **i;

    /* Make an empty authorization directory */
    setup_auth_dir (manager);
  
    /* Start the first display */
    displays = g_key_file_get_string (manager->priv->config, "LightDM", "displays", NULL);
    if (!displays)
        displays = g_strdup ("");
    tokens = g_strsplit (displays, " ", -1);
    g_free (displays);

    for (i = tokens; *i; i++)
    {
        Display *display;
        gchar *default_user, *display_name;
        gint user_timeout;
        XServer *xserver;

        display_name = *i;
        g_debug ("Loading display %s", display_name);

        display = add_display (manager);

        /* Automatically log in or start a greeter session */
        default_user = g_key_file_get_string (manager->priv->config, display_name, "default-user", NULL);
        user_timeout = g_key_file_get_integer (manager->priv->config, display_name, "default-user-timeout", NULL);
        if (user_timeout < 0)
            user_timeout = 0;

        if (default_user)
        {
            display_set_default_user (display, default_user);
            display_set_default_user_timeout (display, user_timeout);
            if (user_timeout == 0)
                g_debug ("Starting session for user %s", default_user);
            else
                g_debug ("Starting session for user %s in %d seconds", default_user, user_timeout);
        }

        xserver = make_xserver (manager, display_name);

        display_set_xserver (display, xserver);
        display_start (display);
        g_object_unref (xserver);
        g_free (default_user);
    }
    g_strfreev (tokens);

    if (g_key_file_get_boolean (manager->priv->config, "xdmcp", "enabled", NULL))
    {
        gchar *key;

        manager->priv->xdmcp_server = xdmcp_server_new ();
        if (g_key_file_has_key (manager->priv->config, "xdmcp", "port", NULL))
        {
            gint port;
            port = g_key_file_get_integer (manager->priv->config, "xdmcp", "port", NULL);
            if (port > 0)
                xdmcp_server_set_port (manager->priv->xdmcp_server, port);
        }
        g_signal_connect (manager->priv->xdmcp_server, "new-session", G_CALLBACK (xdmcp_session_cb), manager);

        key = g_key_file_get_string (manager->priv->config, "xdmcp", "key", NULL);
        if (key)
        {
            guchar data[8];
            string_to_xdm_auth_key (key, data);
            xdmcp_server_set_authentication (manager->priv->xdmcp_server, "XDM-AUTHENTICATION-1", data, 8);
            xdmcp_server_set_authorization (manager->priv->xdmcp_server, "XDM-AUTHORIZATION-1", data, 8);
        }
        g_free (key);

        g_debug ("Starting XDMCP server on UDP/IP port %d", xdmcp_server_get_port (manager->priv->xdmcp_server));
        xdmcp_server_start (manager->priv->xdmcp_server); 
    }
}

static void
display_manager_init (DisplayManager *manager)
{
    manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager, DISPLAY_MANAGER_TYPE, DisplayManagerPrivate);
}

static void
display_manager_finalize (GObject *object)
{
    DisplayManager *self;
    GList *link;

    self = DISPLAY_MANAGER (object);

    if (self->priv->xdmcp_server)
        g_object_unref (self->priv->xdmcp_server);
    for (link = self->priv->displays; link; link = link->next)
        g_object_unref (link->data);
    g_list_free (self->priv->displays);
}

static void
display_manager_class_init (DisplayManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = display_manager_finalize;

    g_type_class_add_private (klass, sizeof (DisplayManagerPrivate));

    signals[DISPLAY_ADDED] =
        g_signal_new ("display-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (DisplayManagerClass, display_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, DISPLAY_TYPE);

    dbus_g_object_type_install_info (DISPLAY_MANAGER_TYPE, &dbus_glib_display_manager_object_info);
}
