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
#include <sys/wait.h>

#include "xserver.h"

enum {
    PROP_0,
    PROP_TYPE,
    PROP_COMMAND,
    PROP_HOSTNAME,
    PROP_PORT,
    PROP_DISPLAY_NUMBER,
    PROP_ADDRESS
};

enum {
    READY,  
    EXITED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

// FIXME: Make a base class and a LocalXServer, RemoteXServer etc

struct XServerPrivate
{  
    /* Type of server */
    XServerType type;

    /* Command to run the X server */
    gchar *command;

    /* TRUE if the xserver has started */
    gboolean ready;

    /* Name of remote host or XDMCP manager */
    gchar *hostname;

    /* UDP/IP port to connect to XDMCP manager */
    guint port;

    /* Auhentication scheme to use */
    gchar *authentication_name;

    /* Auhentication data */  
    guchar *authentication_data;
    gsize authentication_data_length;

    /* Authorization */
    XAuthorization *authorization;
    gchar *authorization_path;
    GFile *authorization_file;

    /* Display number */
    gint display_number;

    /* Cached server address */
    gchar *address;

    /* X process */
    GPid pid;
};

G_DEFINE_TYPE (XServer, xserver, G_TYPE_OBJECT);

static GHashTable *servers = NULL;

void
xserver_handle_signal (GPid pid)
{
    XServer *server;

    server = g_hash_table_lookup (servers, GINT_TO_POINTER (pid));
    if (!server)
        return;

    if (!server->priv->ready)
    {
        server->priv->ready = TRUE;
        g_debug ("Got signal from X server :%d", server->priv->display_number);
        g_signal_emit (server, signals[READY], 0);
    }
}

XServer *
xserver_new (XServerType type, const gchar *hostname, gint display_number)
{
    return g_object_new (XSERVER_TYPE, "type", type, "hostname", hostname, "display-number", display_number, NULL);
}

XServerType
xserver_get_server_type (XServer *server)
{
    return server->priv->type;
}

void
xserver_set_command (XServer *server, const gchar *command)
{
    g_free (server->priv->command);
    server->priv->command = g_strdup (command);
}

const gchar *
xserver_get_command (XServer *server)
{
    return server->priv->command;
}

void
xserver_set_port (XServer *server, guint port)
{
    server->priv->port = port;
}

guint xserver_get_port (XServer *server)
{
    return server->priv->port;
}

const gchar *
xserver_get_hostname (XServer *server)
{
    return server->priv->hostname;
}

gint
xserver_get_display_number (XServer *server)
{
    return server->priv->display_number;
}

const gchar *
xserver_get_address (XServer *server)
{
    if (!server->priv->address)
    {
        if (server->priv->type == XSERVER_TYPE_REMOTE)
            server->priv->address = g_strdup_printf("%s:%d", server->priv->hostname, server->priv->display_number);
        else
            server->priv->address = g_strdup_printf(":%d", server->priv->display_number);
    }  

    return server->priv->address;
}

void
xserver_set_authentication (XServer *server, const gchar *name, const guchar *data, gsize data_length)
{
    g_free (server->priv->authentication_name);
    server->priv->authentication_name = g_strdup (name);
    g_free (server->priv->authentication_data);
    server->priv->authentication_data = g_malloc (data_length);
    server->priv->authentication_data_length = data_length;
    memcpy (server->priv->authentication_data, data, data_length);
}

const gchar *
xserver_get_authentication_name (XServer *server)
{
    return server->priv->authentication_name;
}

const guchar *
xserver_get_authentication_data (XServer *server)
{
    return server->priv->authentication_data;
}

gsize
xserver_get_authentication_data_length (XServer *server)
{
    return server->priv->authentication_data_length;
}

void
xserver_set_authorization (XServer *server, XAuthorization *authorization, const gchar *path)
{
    server->priv->authorization = authorization;
    server->priv->authorization_path = g_strdup (path);
}

XAuthorization *
xserver_get_authorization (XServer *server)
{
    return server->priv->authorization;
}

static void
xserver_watch_cb (GPid pid, gint status, gpointer data)
{
    XServer *server = data;

    if (WIFEXITED (status))
        g_debug ("XServer exited with return value %d", WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        g_debug ("XServer terminated with signal %d", WTERMSIG (status));

    g_hash_table_remove (servers, GINT_TO_POINTER (server->priv->pid));

    server->priv->pid = 0;

    g_signal_emit (server, signals[EXITED], 0);
}

static void
xserver_fork_cb (gpointer data)
{
    /* Clear USR1 handler so the server will signal us when ready */
    signal (SIGUSR1, SIG_IGN);
}

gboolean
xserver_start (XServer *server)
{
    GError *error = NULL;
    gboolean result;
    GString *command;
    gint argc;
    gchar **argv;
    gchar **env;
    gint n_env = 0;
    gchar *env_string;
    //gint xserver_stdin, xserver_stdout, xserver_stderr;

    g_return_val_if_fail (server->priv->pid == 0, FALSE);
 
    /* Don't need to do anything if a remote server */
    if (server->priv->type == XSERVER_TYPE_REMOTE)
    {
        server->priv->ready = TRUE;
        g_signal_emit (server, signals[READY], 0);
        return TRUE;
    }

    // FIXME: Do these need to be freed?
    env = g_malloc (sizeof (gchar *) * 3);
    if (getenv ("DISPLAY"))
        env[n_env++] = g_strdup_printf ("DISPLAY=%s", getenv ("DISPLAY"));
    if (getenv ("XAUTHORITY"))
        env[n_env++] = g_strdup_printf ("XAUTHORITY=%s", getenv ("XAUTHORITY"));
    env[n_env] = NULL;

    /* Write the authorization file */
    if (server->priv->authorization)
    {
        GError *error = NULL;

        server->priv->authorization_file = xauth_write (server->priv->authorization, server->priv->authorization_path, &error);
        if (!server->priv->authorization_file)
            g_warning ("Failed to write authorization: %s", error->message);
        g_clear_error (&error);
    }

    command = g_string_new (server->priv->command);
    g_string_append_printf (command, " :%d", server->priv->display_number);
    g_string_append (command, " -nr");           /* No root background */
    //g_string_append_printf (command, " vt%d");

    if (server->priv->authorization)
         g_string_append_printf (command, " -auth %s", server->priv->authorization_path);

    if (server->priv->type == XSERVER_TYPE_LOCAL_TERMINAL)
    {
        if (server->priv->port != 0)
            g_string_append_printf (command, " -port %d", server->priv->port);
        g_string_append_printf (command, " -query %s", server->priv->hostname);
        if (strcmp (server->priv->authentication_name, "XDM-AUTHENTICATION-1") == 0 && server->priv->authentication_data_length > 0)
        {
            GString *cookie;
            gsize i;

            cookie = g_string_new ("0x");
            for (i = 0; i < server->priv->authentication_data_length; i++)
                g_string_append_printf (cookie, "%02X", server->priv->authentication_data[i]);
            g_string_append_printf (command, " -cookie %s", cookie->str);
            g_string_free (cookie, TRUE);
        }
    }
    else
        g_string_append (command, " -nolisten tcp");

    env_string = g_strjoinv (" ", env);
    g_debug ("Launching X Server: %s %s", env_string, command->str);
    g_free (env_string);

    result = g_shell_parse_argv (command->str, &argc, &argv, &error);
    g_string_free (command, TRUE);
    if (!result)
        g_warning ("Failed to parse X server command line: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    result = g_spawn_async/*_with_pipes*/ (NULL, /* Working directory */
                                       argv,
                                       env,
                                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                       xserver_fork_cb,
                                       server,
                                       &server->priv->pid,
                                       //&xserver_stdin, &xserver_stdout, &xserver_stderr,
                                       &error);
    g_strfreev (argv);
    if (!result)
        g_warning ("Unable to create display: %s", error->message);
    else
    {
        g_debug ("Waiting for signal from X server :%d", server->priv->display_number);
        g_hash_table_insert (servers, GINT_TO_POINTER (server->priv->pid), g_object_ref (server));
        g_child_watch_add (server->priv->pid, xserver_watch_cb, server);
    }
    g_clear_error (&error);

    return server->priv->pid != 0;
}

static void
xserver_init (XServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_TYPE, XServerPrivate);
    server->priv->command = g_strdup (XSERVER_BINARY);
    server->priv->authentication_name = g_strdup ("");
}

static void
xserver_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
    XServer *self;

    self = XSERVER (object);

    switch (prop_id) {
    case PROP_TYPE:
        self->priv->type = g_value_get_int (value);
        break;
    case PROP_COMMAND:
        xserver_set_command (self, g_value_get_string (value));
        break;
    case PROP_HOSTNAME:
        self->priv->hostname = g_strdup (g_value_get_string (value));
        break;
    case PROP_PORT:
        self->priv->port = g_value_get_int (value);
        break;
    case PROP_DISPLAY_NUMBER:
        self->priv->display_number = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}


static void
xserver_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
    XServer *self;

    self = XSERVER (object);

    switch (prop_id) {
    case PROP_TYPE:
        g_value_set_int (value, self->priv->type);
        break;
    case PROP_COMMAND:
        g_value_set_string (value, self->priv->command);
        break;
    case PROP_HOSTNAME:
        g_value_set_string (value, self->priv->hostname);
        break;
    case PROP_PORT:
        g_value_set_int (value, self->priv->port);
        break;
    case PROP_DISPLAY_NUMBER:
        g_value_set_int (value, self->priv->display_number);
        break;
    case PROP_ADDRESS:
        g_value_set_string (value, xserver_get_address (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
xserver_finalize (GObject *object)
{
    XServer *self;

    self = XSERVER (object);
  
    if (self->priv->pid)
        kill (self->priv->pid, SIGTERM);

    g_free (self->priv->command);
    g_free (self->priv->hostname);
    g_free (self->priv->authentication_name);
    g_free (self->priv->authentication_data);
    g_free (self->priv->address);
    if (self->priv->authorization)
        g_object_unref (self->priv->authorization);
    g_free (self->priv->authorization_path);
    if (self->priv->authorization_file)
    {
        g_file_delete (self->priv->authorization_file, NULL, NULL);
        g_object_unref (self->priv->authorization_file);
    }
}

static void
xserver_class_init (XServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = xserver_set_property;
    object_class->get_property = xserver_get_property;
    object_class->finalize = xserver_finalize;  

    g_type_class_add_private (klass, sizeof (XServerPrivate));

    g_object_class_install_property (object_class,
                                     PROP_TYPE,
                                     g_param_spec_int ("type",
                                                       "type",
                                                       "X Server type",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_COMMAND,
                                     g_param_spec_string ("command",
                                                          "command",
                                                          "Command to launch the X server",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     PROP_HOSTNAME,
                                     g_param_spec_string ("hostname",
                                                          "hostname",
                                                          "Server hostname",
                                                          NULL,
                                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_DISPLAY_NUMBER,
                                     g_param_spec_int ("display-number",
                                                       "display-number",
                                                       "Server display number",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_PORT,
                                     g_param_spec_int ("port",
                                                       "port",
                                                       "UDP/IP port to connect XDMCP on",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_ADDRESS,
                                     g_param_spec_string ("address",
                                                          "address",
                                                          "Server address",
                                                          NULL,
                                                          G_PARAM_READABLE));

    signals[READY] =
        g_signal_new ("ready",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XServerClass, ready),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    signals[EXITED] =
        g_signal_new ("exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XServerClass, exited),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    servers = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
}
