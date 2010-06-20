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

#include <sys/wait.h>

#include "xserver.h"

enum {
    PROP_0,
    PROP_CONFIG,
    PROP_INDEX
};

enum {
    EXITED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct XServerPrivate
{
    GKeyFile *config;

    gint index;

    /* Display device */
    gchar *display; // e.g. :0
  
    /* X process */
    GPid pid;
};

G_DEFINE_TYPE (XServer, xserver, G_TYPE_OBJECT);

XServer *
xserver_new (GKeyFile *config, gint index)
{
    return g_object_new (XSERVER_TYPE, "config", config, "index", index, NULL);
}

gint
xserver_get_index (XServer *server)
{
    return server->priv->index;
}

const gchar *xserver_get_display (XServer *server)
{
    return server->priv->display;
}

static void
xserver_watch_cb (GPid pid, gint status, gpointer data)
{
    XServer *server = data;

    if (WIFEXITED (status))
        g_debug ("XServer exited with return value %d", WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        g_debug ("XServer terminated with signal %d", WTERMSIG (status));

    server->priv->pid = 0;

    g_signal_emit (server, signals[EXITED], 0);
}

gboolean
xserver_start (XServer *server)
{
    GError *error = NULL;
    gboolean result;
    gchar *xserver_binary;
    GString *command;
    gint argc;
    gchar **argv;
    gchar *env[] = { NULL };
    gint xserver_stdin, xserver_stdout, xserver_stderr;

    xserver_binary = g_key_file_get_value (server->priv->config, "LightDM", "xserver", NULL);
    if (!xserver_binary)
        xserver_binary = g_strdup (XSERVER_BINARY);
    command = g_string_new (xserver_binary);
    g_string_append_printf (command, " %s", server->priv->display);
    g_string_append (command, " -nolisten tcp"); /* Disable TCP/IP connections */
    g_string_append (command, " -nr");           /* No root background */
    //g_string_append_printf (command, " vt%d");
    g_free (xserver_binary);

    g_debug ("Launching X Server: %s", command->str);

    result = g_shell_parse_argv (command->str, &argc, &argv, &error);
    g_string_free (command, TRUE);
    if (!result)
        g_error ("Failed to parse X server command line: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    result = g_spawn_async_with_pipes (NULL, /* Working directory */
                                       argv,
                                       env,
                                       G_SPAWN_DO_NOT_REAP_CHILD,
                                       NULL, NULL,
                                       &server->priv->pid,
                                       &xserver_stdin, &xserver_stdout, &xserver_stderr,
                                       &error);
    g_strfreev (argv);
    if (!result)
        g_warning ("Unable to create display: %s", error->message);
    else
        g_child_watch_add (server->priv->pid, xserver_watch_cb, server);
    g_clear_error (&error);

    return server->priv->pid != 0;
}

static void
xserver_init (XServer *server)
{
    server->priv = G_TYPE_INSTANCE_GET_PRIVATE (server, XSERVER_TYPE, XServerPrivate);
}

static void
xserver_set_property(GObject      *object,
                     guint         prop_id,
                     const GValue *value,
                     GParamSpec   *pspec)
{
    XServer *self;

    self = XSERVER (object);

    switch (prop_id) {
    case PROP_CONFIG:
        self->priv->config = g_value_get_pointer (value);
        break;
    case PROP_INDEX:
        self->priv->index = g_value_get_int (value);
        self->priv->display = g_strdup_printf (":%d", self->priv->index);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}


static void
xserver_get_property(GObject    *object,
                     guint       prop_id,
                     GValue     *value,
                     GParamSpec *pspec)
{
    XServer *self;

    self = XSERVER (object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_pointer (value, self->priv->config);
        break;
    case PROP_INDEX:
        g_value_set_int (value, self->priv->index);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
xserver_class_init (XServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = xserver_set_property;
    object_class->get_property = xserver_get_property;

    g_type_class_add_private (klass, sizeof (XServerPrivate));

    g_object_class_install_property (object_class,
                                     PROP_CONFIG,
                                     g_param_spec_pointer ("config",
                                                           "config",
                                                           "Configuration",
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property (object_class,
                                     PROP_INDEX,
                                     g_param_spec_int ("index",
                                                       "index",
                                                       "Server index",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    signals[EXITED] =
        g_signal_new ("exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (XServerClass, exited),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
