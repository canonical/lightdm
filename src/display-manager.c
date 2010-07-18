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

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dbus/dbus-glib.h>

#include "display-manager.h"
#include "display-manager-glue.h"
#include "xdmcp-server.h"
#include "xserver.h"
#include "theme.h"

enum {
    PROP_0,
    PROP_CONFIG,
};

enum {
    DISPLAY_ADDED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct DisplayManagerPrivate
{
    GKeyFile *config;
  
    gchar *auth_dir;

    gboolean test_mode;

    GList *displays;
  
    XDMCPServer *xdmcp_server;
  
    guint auth_counter;
};

G_DEFINE_TYPE (DisplayManager, display_manager, G_TYPE_OBJECT);

DisplayManager *
display_manager_new (GKeyFile *config)
{
    return g_object_new (DISPLAY_MANAGER_TYPE, "config", config, NULL);
}

#include <xcb/xcb.h>
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
start_session_cb (Display *display, Session *session, DisplayManager *manager)
{
    gchar *string;
    XAuthorization *authorization;

    /* Connect using the session bus */
    if (manager->priv->test_mode)
    {
        session_set_env (session, "DBUS_SESSION_BUS_ADDRESS", getenv ("DBUS_SESSION_BUS_ADDRESS"));
        session_set_env (session, "LDM_BUS", "SESSION");
    }

    /* Address for greeter to connect to */
    string = g_strdup_printf ("/org/gnome/LightDisplayManager/Display%d", display_get_index (display));
    session_set_env (session, "LDM_DISPLAY", string);

    authorization = xserver_get_authorization (display_get_xserver (display));
    if (authorization)
    {
        gchar *path;

        path = get_authorization_path (manager);
        session_set_authorization (session, authorization, path);
        g_free (path);
    }

    g_free (string);
}

static Display *
add_display (DisplayManager *manager)
{
    Display *display;
    gchar *value;

    display = display_new (g_list_length (manager->priv->displays));
    g_signal_connect (display, "start-session", G_CALLBACK (start_session_cb), manager);

    if (manager->priv->test_mode)
        display_set_greeter_user (display, NULL);

    value = g_key_file_get_value (manager->priv->config, "Greeter", "user", NULL);
    if (value)
        display_set_greeter_user (display, value);
    g_free (value);
    value = g_key_file_get_value (manager->priv->config, "Greeter", "theme", NULL);
    if (value)
        display_set_greeter_theme (display, value);
    g_free (value);

    manager->priv->displays = g_list_append (manager->priv->displays, display);

    g_signal_emit (manager, signals[DISPLAY_ADDED], 0, display);

    return display;
}

gboolean
display_manager_switch_to_user (DisplayManager *manager, char *username, GError *error)
{
    return TRUE;
}

static void
xdmcp_session_cb (XDMCPServer *server, XDMCPSession *session, DisplayManager *manager)
{
    Display *display;
    gchar *address, *path;
    XServer *xserver;
    XAuthorization *authorization;

    display = add_display (manager);
    address = g_inet_address_to_string (G_INET_ADDRESS (xdmcp_session_get_address (session)));
    xserver = xserver_new (XSERVER_TYPE_REMOTE, address, xdmcp_session_get_display_number (session));
    authorization = xauth_new (xdmcp_session_get_authorization_name (session),
                               xdmcp_session_get_authorization_data (session),
                               xdmcp_session_get_authorization_data_length (session));
    path = get_authorization_path (manager);
    xserver_set_authorization (xserver, authorization, path);
    display_start (display, xserver, NULL, 0);
    g_object_unref (xserver);
    g_free (address);
    g_free (path);
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

static void
setup_auth_dir (DisplayManager *manager)
{
    GDir *dir;
    GError *error = NULL;

    g_mkdir_with_parents (manager->priv->auth_dir, S_IRWXU);
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
        gchar *default_user, *xdmcp_manager, *display_name;
        gint user_timeout;
        guint display_number;
        XServer *xserver;

        display_name = *i;
        g_debug ("Loading display %s", display_name);

        display = add_display (manager);

        /* Automatically log in or start a greeter session */  
        default_user = g_key_file_get_value (manager->priv->config, "Default User", "name", NULL);
        //FIXME default_user_session = g_key_file_get_value (manager->priv->config, "Default User", "session", NULL); // FIXME
        user_timeout = g_key_file_get_integer (manager->priv->config, "Default User", "timeout", NULL);
        if (user_timeout < 0)
            user_timeout = 0;

        if (default_user)
        {
            if (user_timeout == 0)
                g_debug ("Starting session for user %s", default_user);
            else
                g_debug ("Starting session for user %s in %d seconds", default_user, user_timeout);
        }

        if (g_key_file_has_key (manager->priv->config, display_name, "display-number", NULL))
            display_number = g_key_file_get_integer (manager->priv->config, display_name, "display-number", NULL);
        else
            display_number = get_free_display_number (manager);

        xdmcp_manager = g_key_file_get_string (manager->priv->config, display_name, "xdmcp-manager", NULL);
        if (xdmcp_manager)
        {
            gint port;
            gchar *key;

            xserver = xserver_new (XSERVER_TYPE_LOCAL_TERMINAL, xdmcp_manager, display_number);
            port = g_key_file_get_integer (manager->priv->config, display_name, "xdmcp-port", NULL);
            if (port > 0)
                xserver_set_port (xserver, port);
            key = g_key_file_get_string (manager->priv->config, display_name, "key", NULL);
            if (key)
            {
                guchar data[8];
                gchar *path;
                XAuthorization *authorization;

                string_to_xdm_auth_key (key, data);
                xserver_set_authentication (xserver, "XDM-AUTHENTICATION-1", data, 8);

                authorization = xauth_new ("XDM-AUTHORIZATION-1", data, 8);
                path = get_authorization_path (manager);
                xserver_set_authorization (xserver, authorization, path);

              g_free (path);
            }
        }
        else
        {
            gchar *path;
            XAuthorization *authorization;

            xserver = xserver_new (XSERVER_TYPE_LOCAL, NULL, display_number);

            authorization = xauth_new_cookie ();
            path = get_authorization_path (manager);
            xserver_set_authorization (xserver, authorization, path);
            g_free (path);
        }
        g_free (xdmcp_manager);

        if (manager->priv->test_mode)
            xserver_set_command (xserver, "Xephyr");

        display_start (display, xserver, default_user, user_timeout);
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
        g_signal_connect (manager->priv->xdmcp_server, "session-added", G_CALLBACK (xdmcp_session_cb), manager);

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
display_manager_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    DisplayManager *self;

    self = DISPLAY_MANAGER (object);

    switch (prop_id) {
    case PROP_CONFIG:
        self->priv->config = g_value_get_pointer (value);
        self->priv->test_mode = g_key_file_get_boolean (self->priv->config, "LightDM", "test-mode", NULL);
        if (self->priv->test_mode)
            self->priv->auth_dir = g_build_filename (g_get_user_cache_dir (), "lightdm", "authority", NULL);
        else
            self->priv->auth_dir = g_strdup (XAUTH_DIR);       
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
display_manager_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    DisplayManager *self;

    self = DISPLAY_MANAGER (object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_pointer (value, self->priv->config);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
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

    object_class->set_property = display_manager_set_property;
    object_class->get_property = display_manager_get_property;
    object_class->finalize = display_manager_finalize;

    g_type_class_add_private (klass, sizeof (DisplayManagerPrivate));

    g_object_class_install_property (object_class,
                                     PROP_CONFIG,
                                     g_param_spec_pointer ("config",
                                                           "config",
                                                           "Configuration",
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  
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
