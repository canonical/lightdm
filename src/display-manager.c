/*
 * Copyright (C) 2010-2011 Robert Ancell.
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
#include <errno.h>
#include <sys/stat.h>
#include <xcb/xcb.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/vt.h>
#endif

#include "display-manager.h"
#include "configuration.h"
#include "user.h"
#include "guest-manager.h"
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
    /* Directory to store authorization files */
    gchar *auth_dir;

    /* Counter to generate unique authorization file names */
    guint auth_counter;

    /* Directory to write log files to */
    gchar *log_dir;

    /* The displays being managed */
    GList *displays;

    /* XDMCP server */
    XDMCPServer *xdmcp_server;
};

G_DEFINE_TYPE (DisplayManager, display_manager, G_TYPE_OBJECT);

DisplayManager *
display_manager_new (void)
{
    DisplayManager *self = g_object_new (DISPLAY_MANAGER_TYPE, NULL);

    self->priv->auth_dir = config_get_string (config_get_instance (), "LightDM", "authorization-directory");
    self->priv->log_dir = config_get_string (config_get_instance (), "LightDM", "log-directory");

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
    if (getpid () != 0)
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
    gchar *log_filename = NULL;
    XAuthorization *authorization;

    /* Connect using the session bus */
    if (getpid () != 0)
    {
        child_process_set_env (CHILD_PROCESS (session), "DBUS_SESSION_BUS_ADDRESS", getenv ("DBUS_SESSION_BUS_ADDRESS"));
        child_process_set_env (CHILD_PROCESS (session), "XDG_SESSION_COOKIE", getenv ("XDG_SESSION_COOKIE"));
        child_process_set_env (CHILD_PROCESS (session), "LDM_BUS", "SESSION");
    }

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
        log_filename = g_build_filename (manager->priv->log_dir, filename, NULL);
        g_free (filename);
    }
    else
    {
        // FIXME: Copy old error file
        User *user = user_get_by_name (session_get_username (session));
        if (user)
        {
            log_filename = g_build_filename (user_get_home_directory (user), ".xsession-errors", NULL);
            g_object_unref (user);
        }
        else
            g_warning ("Failed to get user info for user '%s'", session_get_username (session));
    }

    if (log_filename)
    {      
        g_debug ("Logging to %s", log_filename);
        child_process_set_log_file (CHILD_PROCESS (session), log_filename);
        g_free (log_filename);
    }
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

static gint
get_vt (DisplayManager *manager, gchar *config_section)
{
    gchar *vt;
    gboolean use_active = FALSE;
#ifdef __linux__
    gint console_fd;
#endif
    int number = -1;

    if (getpid () != 0)
        return -1;

    vt = config_get_string (config_get_instance (), config_section, "vt");
    if (vt)
    {
        if (strcmp (vt, "active") == 0)
            use_active = TRUE;
        else
            number = atoi (vt);
        g_free (vt);

        if (number >= 0)
            return number;
    }

#ifdef __linux__
    console_fd = g_open ("/dev/console", O_RDONLY | O_NOCTTY);
    if (console_fd < 0)
    {
        g_warning ("Error opening /dev/console: %s", strerror (errno));
        return -1;
    }

    if (use_active)
    {
        struct vt_stat console_state = { 0 };
        if (ioctl (console_fd, VT_GETSTATE, &console_state) < 0)
            g_warning ("Error using VT_GETSTATE on /dev/console: %s", strerror (errno));
        else
            number = console_state.v_active;
    }
    else
    {      
        if (ioctl (console_fd, VT_OPENQRY, &number) < 0)
            g_warning ("Error using VT_OPENQRY on /dev/console: %s", strerror (errno));
    }

    close (console_fd);
#else
    number = -1;
#endif    

    return number;  
}

static XServer *
make_xserver (DisplayManager *manager, gchar *config_section)
{
    gint display_number, vt;
    XServer *xserver;
    XAuthorization *authorization = NULL;
    gchar *xdmcp_manager, *filename, *path, *command, *xserver_section = NULL;

    if (config_section && config_has_key (config_get_instance (), config_section, "display-number"))
        display_number = config_get_integer (config_get_instance (), config_section, "display-number");
    else
        display_number = get_free_display_number (manager);

    xdmcp_manager = config_section ? config_get_string (config_get_instance (), config_section, "xdmcp-manager") : NULL;
    if (xdmcp_manager)
    {
        gint port;
        gchar *key;

        xserver = xserver_new (XSERVER_TYPE_LOCAL_TERMINAL, xdmcp_manager, display_number);

        port = config_get_integer (config_get_instance (), config_section, "xdmcp-port");
        if (port > 0)
            xserver_set_port (xserver, port);
        key = config_get_string (config_get_instance (), config_section, "key");
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

    command = config_get_string (config_get_instance (), "LightDM", "default-xserver-command");
    xserver_set_command (xserver, command);
    g_free (command);

    path = get_authorization_path (manager);
    xserver_set_authorization (xserver, authorization, path);
    g_object_unref (authorization);
    g_free (path);

    filename = g_strdup_printf ("%s.log", xserver_get_address (xserver));
    path = g_build_filename (manager->priv->log_dir, filename, NULL);
    g_debug ("Logging to %s", path);
    child_process_set_log_file (CHILD_PROCESS (xserver), path);
    g_free (filename);
    g_free (path);

    vt = get_vt (manager, config_section);
    if (vt >= 0)
    {
        g_debug ("Starting on /dev/tty%d", vt);
        xserver_set_vt (xserver, vt);
    }

    /* Get the X server configuration */
    if (config_section)
        xserver_section = config_get_string (config_get_instance (), config_section, "xserver");
    if (!xserver_section)
        xserver_section = config_get_string (config_get_instance (), "LightDM", "xserver");

    if (xserver_section)
    {
        gchar *xserver_command, *xserver_layout, *xserver_config_file;

        g_debug ("Using X server configuration '%s' for display '%s'", xserver_section, config_section ? config_section : "<anonymous>");

        xserver_command = config_get_string (config_get_instance (), xserver_section, "command");
        if (xserver_command)
            xserver_set_command (xserver, xserver_command);
        g_free (xserver_command);

        xserver_layout = config_get_string (config_get_instance (), xserver_section, "layout");
        if (xserver_layout)
            xserver_set_layout (xserver, xserver_layout);
        g_free (xserver_layout);

        xserver_config_file = config_get_string (config_get_instance (), xserver_section, "config-file");
        if (xserver_config_file)
            xserver_set_config_file (xserver, xserver_config_file);
        g_free (xserver_config_file);

        g_free (xserver_section);
    }

    if (config_get_boolean (config_get_instance (), "LightDM", "use-xephyr"))
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

    value = config_get_string (config_get_instance (), "LightDM", "session-wrapper");
    if (value)
        display_set_session_wrapper (display, value);
    g_free (value);

    if (getpid () != 0)
        display_set_greeter_user (display, NULL);

    manager->priv->displays = g_list_append (manager->priv->displays, display);

    g_signal_emit (manager, signals[DISPLAY_ADDED], 0, display);

    return display;
}

Display *
display_manager_add_display (DisplayManager *manager)
{
    Display *display;
    XServer *xserver;

    g_debug ("Starting new display");
    display = add_display (manager);
    xserver = make_xserver (manager, NULL);
    display_set_xserver (display, xserver);
    display_start (display);
    g_object_unref (xserver);

    return display;
}

void
display_manager_switch_to_user (DisplayManager *manager, char *username)
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
            return;
        }
    }

    g_debug ("Starting new display for user %s", username);
    display = add_display (manager);
    xserver = make_xserver (manager, NULL);
    display_set_xserver (display, xserver);
    display_start (display);
    g_object_unref (xserver);
}

void
display_manager_switch_to_guest (DisplayManager *manager)
{
    // fixme  
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

static gboolean
plymouth_run_command (const gchar *command, gint *exit_status)
{
    gchar *command_line;
    gboolean result;
    GError *error = NULL;

    command_line = g_strdup_printf ("/bin/plymouth %s", command);  
    result = g_spawn_command_line_sync (command_line, NULL, NULL, exit_status, &error);
    g_free (command_line);

    if (!result)
        g_debug ("Could not run %s: %s", command_line, error->message);
    g_clear_error (&error);

    return result;
}

static gboolean
plymouth_command_returns_true (gchar *command)
{
    gint exit_status;
    if (!plymouth_run_command (command, &exit_status))
        return FALSE;
    return WIFEXITED (exit_status) && WEXITSTATUS (exit_status) == 0;
}

static void
stop_plymouth_cb (XServer *xserver, DisplayManager *manager)
{
    g_debug ("Stopping Plymouth, display replacing it has started");
    plymouth_run_command ("quit --retain-splash", NULL);
}

static void
stop_plymouth_due_to_failure_cb (XServer *xserver, int status_of_signum, DisplayManager *manager)
{
    /* Check if Plymouth is even running (we may have already transitioned to it */
    if (!plymouth_command_returns_true ("--ping"))
        return;

    g_debug ("Stopping Plymouth, error starting display");
    plymouth_run_command ("quit", NULL);   
}

void
display_manager_start (DisplayManager *manager)
{
    gchar *seats;
    gchar **tokens, **i;
    gboolean plymouth_is_running, plymouth_on_active_vt = FALSE, plymouth_being_replaced = FALSE;

    /* Make an empty authorization directory */
    setup_auth_dir (manager);

    /* Load the static display entries */
    seats = config_get_string (config_get_instance (), "LightDM", "seats");
    /* Fallback to the old name for seats, this will be removed before 1.0 */
    if (!seats)
        seats = config_get_string (config_get_instance (), "LightDM", "displays");
    if (!seats)
        seats = g_strdup ("");
    tokens = g_strsplit (seats, " ", -1);
    g_free (seats);

    /* Check if Plymouth is running and start to deactivate it */
    plymouth_is_running = plymouth_command_returns_true ("--ping");
    if (plymouth_is_running)
    {
        /* Check if running on the active VT */
        plymouth_on_active_vt = plymouth_command_returns_true ("--has-active-vt");

        /* Deactivate stops Plymouth from drawing, as we are about to start an X server to replace it */
        plymouth_run_command ("deactivate", NULL);
    }

    /* Start each static display */
    for (i = tokens; *i; i++)
    {
        Display *display;
        gchar *value, *default_user, *display_name;
        gint user_timeout;
        XServer *xserver;
        gboolean replaces_plymouth = FALSE;

        display_name = *i;
        g_debug ("Loading display %s", display_name);

        display = add_display (manager);

        /* If this is starting on the active VT, then this display will replace Plymouth */
        if (plymouth_on_active_vt && !plymouth_being_replaced)
        {
            gchar *vt;
            vt = config_get_string (config_get_instance (), display_name, "vt");
            if (vt && strcmp (vt, "active") == 0)
            {
                plymouth_being_replaced = TRUE;
                replaces_plymouth = TRUE;
            }
            g_free (vt);
        }

        value = config_get_string (config_get_instance (), display_name, "session");
        if (value)
            display_set_default_session (display, value);
        g_free (value);
        value = config_get_string (config_get_instance (), display_name, "greeter-user");
        if (value)
            display_set_greeter_user (display, value);
        g_free (value);
        value = config_get_string (config_get_instance (), display_name, "greeter-theme");
        if (value)
            display_set_greeter_theme (display, value);
        g_free (value);
        value = config_get_string (config_get_instance (), display_name, "pam-service");
        if (value)
            display_set_pam_service (display, value);
        g_free (value);
        value = config_get_string (config_get_instance (), display_name, "pam-autologin-service");
        if (value)
            display_set_pam_autologin_service (display, value);
        g_free (value);

        /* Automatically log in or start a greeter session */
        default_user = config_get_string (config_get_instance (), display_name, "default-user");
        user_timeout = config_get_integer (config_get_instance (), display_name, "default-user-timeout");
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

        /* Stop Plymouth when the X server starts/fails */
        if (replaces_plymouth)
        {
            g_debug ("Display %s will replace Plymouth", display_name);
            xserver_set_no_root (xserver, TRUE);
            g_signal_connect (xserver, "ready", G_CALLBACK (stop_plymouth_cb), manager);
            g_signal_connect (xserver, "exited", G_CALLBACK (stop_plymouth_due_to_failure_cb), manager);
            g_signal_connect (xserver, "terminated", G_CALLBACK (stop_plymouth_due_to_failure_cb), manager);
        }

        /* Start it up! */
        if (!display_start (display))
        {
            g_warning ("Failed to start static display %s", display_name);
            if (replaces_plymouth)
            {
                g_debug ("Stopping Plymouth, display failed to start");
                plymouth_run_command ("quit", NULL);
            }
        }

        g_object_unref (xserver);
        g_free (default_user);
    }
    g_strfreev (tokens);

    /* Stop Plymouth if we're not expecting an X server to replace it */
    if (plymouth_is_running && !plymouth_being_replaced)
    {
        g_debug ("Stopping Plymouth, no displays replace it");
        plymouth_run_command ("quit", NULL);
    }

    if (config_get_boolean (config_get_instance (), "xdmcp", "enabled"))
    {
        gchar *key;

        manager->priv->xdmcp_server = xdmcp_server_new ();
        if (config_has_key (config_get_instance (), "xdmcp", "port"))
        {
            gint port;
            port = config_get_integer (config_get_instance (), "xdmcp", "port");
            if (port > 0)
                xdmcp_server_set_port (manager->priv->xdmcp_server, port);
        }
        g_signal_connect (manager->priv->xdmcp_server, "new-session", G_CALLBACK (xdmcp_session_cb), manager);

        key = config_get_string (config_get_instance (), "xdmcp", "key");
        if (key)
        {
            guchar data[8];
            string_to_xdm_auth_key (key, data);
            xdmcp_server_set_authentication (manager->priv->xdmcp_server, "XDM-AUTHENTICATION-1", data, 8);
            xdmcp_server_set_authorization (manager->priv->xdmcp_server, "XDM-AUTHORIZATION-1", data, 8);
            g_free (key);
        }
        else
            xdmcp_server_set_authorization (manager->priv->xdmcp_server, "MIT-MAGIC-COOKIE-1", NULL, 0);

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

    G_OBJECT_CLASS (display_manager_parent_class)->finalize (object);
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
}
