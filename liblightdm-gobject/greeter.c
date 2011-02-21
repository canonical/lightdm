/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/utsname.h>

#include <gio/gdesktopappinfo.h>
#include <security/pam_appl.h>
#include <libxklavier/xklavier.h>

#include "greeter.h"
#include "greeter-protocol.h"

enum {
    PROP_0,
    PROP_HOSTNAME,
    PROP_NUM_USERS,
    PROP_USERS,
    PROP_DEFAULT_LANGUAGE,
    PROP_LAYOUTS,
    PROP_LAYOUT,
    PROP_SESSIONS,
    PROP_DEFAULT_SESSION,
    PROP_TIMED_LOGIN_USER,
    PROP_TIMED_LOGIN_DELAY,
    PROP_AUTHENTICATION_USER,
    PROP_IS_AUTHENTICATED,
    PROP_CAN_SUSPEND,
    PROP_CAN_HIBERNATE,
    PROP_CAN_RESTART,
    PROP_CAN_SHUTDOWN
};

enum {
    CONNECTED,
    SHOW_PROMPT,
    SHOW_MESSAGE,
    SHOW_ERROR,
    AUTHENTICATION_COMPLETE,
    TIMED_LOGIN,
    QUIT,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct _LdmGreeterPrivate
{
    GDBusConnection *lightdm_bus;

    GDBusConnection *system_bus;

    GDBusProxy *session_proxy, *user_proxy;

    GIOChannel *to_server_channel, *from_server_channel;

    Display *display;

    gchar *hostname;

    gchar *theme;
    GKeyFile *theme_file;

    gboolean have_users;
    GList *users;

    gboolean have_languages;
    GList *languages;

    gchar *default_layout;
    XklEngine *xkl_engine;
    XklConfigRec *xkl_config;
    gboolean have_layouts;
    GList *layouts;
    gchar *layout;

    gboolean have_sessions;
    GList *sessions;
    gchar *default_session;

    gchar *authentication_user;
    gboolean is_authenticated;

    gchar *timed_user;
    gint login_delay;
    guint login_timeout;
};

G_DEFINE_TYPE (LdmGreeter, ldm_greeter, G_TYPE_OBJECT);

/**
 * ldm_greeter_new:
 *
 * Create a new greeter.
 *
 * Return value: the new #LdmGreeter
 **/
LdmGreeter *
ldm_greeter_new ()
{
    return g_object_new (LDM_TYPE_GREETER, NULL);
}

static gboolean
timed_login_cb (gpointer data)
{
    LdmGreeter *greeter = data;

    greeter->priv->login_timeout = 0;
    g_signal_emit (G_OBJECT (greeter), signals[TIMED_LOGIN], 0, greeter->priv->timed_user);

    return FALSE;
}

static guint32
read_int (LdmGreeter *greeter)
{
    guint32 value;
    g_io_channel_read_chars (greeter->priv->from_server_channel, (gchar *) &value, sizeof (value), NULL, NULL);
    return value;
}

static void
write_int (LdmGreeter *greeter, guint32 value)
{
    if (g_io_channel_write_chars (greeter->priv->to_server_channel, (const gchar *) &value, sizeof (value), NULL, NULL) != G_IO_STATUS_NORMAL)
        g_warning ("Error writing to server");
}

static gchar *
read_string (LdmGreeter *greeter)
{
    guint32 length;
    gchar *value;

    length = read_int (greeter);
    value = g_malloc (sizeof (gchar *) * (length + 1));
    g_io_channel_read_chars (greeter->priv->from_server_channel, value, length, NULL, NULL);
    value[length] = '\0';

    return value;
}

static void
write_string (LdmGreeter *greeter, const gchar *value)
{
    write_int (greeter, strlen (value));
    g_io_channel_write_chars (greeter->priv->to_server_channel, value, -1, NULL, NULL);
}

static void
flush (LdmGreeter *greeter)
{
    g_io_channel_flush (greeter->priv->to_server_channel, NULL);
}

static void
handle_prompt_authentication (LdmGreeter *greeter)
{
    int n_messages, i;

    n_messages = read_int (greeter);
    g_debug ("Prompt user with %d message(s)", n_messages);

    for (i = 0; i < n_messages; i++)
    {
        int msg_style;
        gchar *msg;

        msg_style = read_int (greeter);
        msg = read_string (greeter);

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
    }
}

static gboolean
from_server_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    LdmGreeter *greeter = data;
    int message, return_code;

    message = read_int (greeter);
    switch (message)
    {
    case GREETER_MESSAGE_CONNECTED:
        greeter->priv->theme = read_string (greeter);
        greeter->priv->default_layout = read_string (greeter);
        greeter->priv->default_session = read_string (greeter);
        greeter->priv->timed_user = read_string (greeter);
        greeter->priv->login_delay = read_int (greeter);

        g_debug ("Connected theme=%s default-layout=%s default-session=%s timed-user=%s login-delay=%d",
                 greeter->priv->theme,
                 greeter->priv->default_layout, greeter->priv->default_session,
                 greeter->priv->timed_user, greeter->priv->login_delay);

        /* Set timeout for default login */
        if (greeter->priv->timed_user[0] != '\0' && greeter->priv->login_delay > 0)
        {
            g_debug ("Logging in as %s in %d seconds", greeter->priv->timed_user, greeter->priv->login_delay);
            greeter->priv->login_timeout = g_timeout_add (greeter->priv->login_delay * 1000, timed_login_cb, greeter);
        }
        g_signal_emit (G_OBJECT (greeter), signals[CONNECTED], 0);
        break;
    case GREETER_MESSAGE_QUIT:
        g_signal_emit (G_OBJECT (greeter), signals[QUIT], 0);
        break;
    case GREETER_MESSAGE_PROMPT_AUTHENTICATION:
        handle_prompt_authentication (greeter);
        break;
    case GREETER_MESSAGE_END_AUTHENTICATION:
        return_code = read_int (greeter);
        g_debug ("Authentication complete with return code %d", return_code);
        greeter->priv->is_authenticated = (return_code == 0);
        if (!greeter->priv->is_authenticated)
        {
            g_free (greeter->priv->authentication_user);
            greeter->priv->authentication_user = NULL;
        }
        g_signal_emit (G_OBJECT (greeter), signals[AUTHENTICATION_COMPLETE], 0);
        break;
    default:
        g_warning ("Unknown message from server: %d", message);
        break;
    }

    return TRUE;
}

/**
 * ldm_greeter_connect:
 * @greeter: The greeter to connect
 *
 * Connects the greeter to the display manager.
 *
 * Return value: TRUE if successfully connected
 **/
gboolean
ldm_greeter_connect (LdmGreeter *greeter)
{
    GError *error = NULL;
    const gchar *bus_address, *fd;
    GBusType bus_type = G_BUS_TYPE_SYSTEM;

    g_return_val_if_fail (greeter != NULL, FALSE);

    greeter->priv->system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!greeter->priv->system_bus)
        g_warning ("Failed to connect to system bus: %s", error->message);
    g_clear_error (&error);
    if (!greeter->priv->system_bus)
        return FALSE;

    bus_address = getenv ("LDM_BUS");
    if (bus_address && strcmp (bus_address, "SESSION") == 0)
        bus_type = G_BUS_TYPE_SESSION;

    greeter->priv->lightdm_bus = g_bus_get_sync (bus_type, NULL, &error);
    if (!greeter->priv->lightdm_bus)
        g_warning ("Failed to connect to LightDM bus: %s", error->message);
    g_clear_error (&error);
    if (!greeter->priv->lightdm_bus)
        return FALSE;

    fd = getenv ("LDM_TO_SERVER_FD");
    if (!fd)
    {
        g_warning ("No LDM_TO_SERVER_FD environment variable");
        return FALSE;
    }
    greeter->priv->to_server_channel = g_io_channel_unix_new (atoi (fd));
    g_io_channel_set_encoding (greeter->priv->to_server_channel, NULL, NULL);

    fd = getenv ("LDM_FROM_SERVER_FD");
    if (!fd)
    {
        g_warning ("No LDM_FROM_SERVER_FD environment variable");
        return FALSE;
    }
    greeter->priv->from_server_channel = g_io_channel_unix_new (atoi (fd));
    g_io_channel_set_encoding (greeter->priv->from_server_channel, NULL, NULL);
    g_io_add_watch (greeter->priv->from_server_channel, G_IO_IN, from_server_cb, greeter);

    greeter->priv->session_proxy = g_dbus_proxy_new_sync (greeter->priv->lightdm_bus,
                                                          G_DBUS_PROXY_FLAGS_NONE,
                                                          NULL,
                                                          "org.lightdm.LightDisplayManager",
                                                          "/org/lightdm/LightDisplayManager/Session",
                                                          "org.lightdm.LightDisplayManager.Session",
                                                          NULL, NULL);
    greeter->priv->user_proxy = g_dbus_proxy_new_sync (greeter->priv->lightdm_bus,
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       NULL,
                                                       "org.lightdm.LightDisplayManager",
                                                       "/org/lightdm/LightDisplayManager/Users",
                                                       "org.lightdm.LightDisplayManager.Users",
                                                       NULL, NULL);

    g_debug ("Connecting to display manager...");
    write_int (greeter, GREETER_MESSAGE_CONNECT);
    flush (greeter);

    return TRUE;
}

/**
 * ldm_greeter_get_hostname:
 * @greeter: a #LdmGreeter
 *
 * Return value: The host this greeter is displaying
 **/
const gchar *
ldm_greeter_get_hostname (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);

    if (!greeter->priv->hostname)
    {
        struct utsname info;
        uname (&info);
        greeter->priv->hostname = g_strdup (info.nodename);
    }

    return greeter->priv->hostname;
}

/**
 * ldm_greeter_get_theme:
 * @greeter: a #LdmGreeter
 *
 * Return value: The theme this greeter is using
 **/
const gchar *
ldm_greeter_get_theme (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->theme;
}

static void
load_theme (LdmGreeter *greeter)
{
    GError *error = NULL;

    if (greeter->priv->theme_file)
        return;

    greeter->priv->theme_file = g_key_file_new ();
    if (!g_key_file_load_from_file (greeter->priv->theme_file, greeter->priv->theme, G_KEY_FILE_NONE, &error))
        g_warning ("Failed to read theme file: %s", error->message);
    g_clear_error (&error);
}

/**
 * ldm_greeter_get_string_property:
 * @greeter: a #LdmGreeter
 * @name: the name of the property to get
 *
 * Return value: The value of this property or NULL if it is not defined
 **/
gchar *
ldm_greeter_get_string_property (LdmGreeter *greeter, const gchar *name)
{
    GError *error = NULL;
    gchar *result;

    g_return_val_if_fail (greeter != NULL, NULL);

    load_theme (greeter);

    result = g_key_file_get_string (greeter->priv->theme_file, "theme", name, &error);
    if (!result)
        g_warning ("Error reading theme property: %s", error->message); // FIXME: Can handle G_KEY_FILE_ERROR_KEY_NOT_FOUND and G_KEY_FILE_ERROR_GROUP_NOT_FOUND
    g_clear_error (&error);

    return result;
}

/**
 * ldm_greeter_get_integer_property:
 * @greeter: a #LdmGreeter
 * @name: the name of the property to get
 *
 * Return value: The value of this property or 0 if it is not defined
 **/
gint
ldm_greeter_get_integer_property (LdmGreeter *greeter, const gchar *name)
{
    GError *error = NULL;
    gint result;

    g_return_val_if_fail (greeter != NULL, 0);

    load_theme (greeter);

    result = g_key_file_get_integer (greeter->priv->theme_file, "theme", name, &error);
    if (!result)
        g_warning ("Error reading theme property: %s", error->message); // FIXME: Can handle G_KEY_FILE_ERROR_KEY_NOT_FOUND and G_KEY_FILE_ERROR_GROUP_NOT_FOUND
    g_clear_error (&error);

    return result;
}

/**
 * ldm_greeter_get_boolean_property:
 * @greeter: a #LdmGreeter
 * @name: the name of the property to get
 *
 * Return value: The value of this property or FALSE if it is not defined
 **/
gboolean
ldm_greeter_get_boolean_property (LdmGreeter *greeter, const gchar *name)
{
    GError *error = NULL;
    gboolean result;

    g_return_val_if_fail (greeter != NULL, FALSE);

    load_theme (greeter);

    result = g_key_file_get_boolean (greeter->priv->theme_file, "theme", name, &error);
    if (!result)
        g_warning ("Error reading theme property: %s", error->message); // FIXME: Can handle G_KEY_FILE_ERROR_KEY_NOT_FOUND and G_KEY_FILE_ERROR_GROUP_NOT_FOUND
    g_clear_error (&error);

    return result;
}

static void
update_users (LdmGreeter *greeter)
{
    GVariant *result, *user_array;
    GVariantIter iter;
    gchar *name, *real_name, *image;
    gboolean logged_in;
    GError *error = NULL;

    if (greeter->priv->have_users)
        return;

    g_debug ("Getting user list...");
    result = g_dbus_proxy_call_sync (greeter->priv->user_proxy,
                                     "GetUsers",
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    if (!result)
        g_warning ("Failed to get users: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    if (!g_variant_is_of_type (result, G_VARIANT_TYPE ("(a(sssb))")))
    {
        g_warning ("Unknown type returned");
        g_variant_unref (result);
        return;
    }
    user_array = g_variant_get_child_value (result, 0);
    g_debug ("Got %zi users", g_variant_n_children (user_array));
    g_variant_iter_init (&iter, user_array);
    while (g_variant_iter_next (&iter, "(&s&s&sb)", &name, &real_name, &image, &logged_in))
    {
        LdmUser *user;

        user = ldm_user_new (greeter, name, real_name, image, logged_in);
        greeter->priv->users = g_list_append (greeter->priv->users, user);
    }
    greeter->priv->have_users = TRUE;

    g_variant_unref (result);
}

/**
 * ldm_greeter_get_num_users:
 * @greeter: a #LdmGreeter
 *
 * Return value: The number of users able to log in
 **/
gint
ldm_greeter_get_num_users (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, 0);
    update_users (greeter);
    return g_list_length (greeter->priv->users);
}

/**
 * ldm_greeter_get_users:
 * @greeter: A #LdmGreeter
 *
 * Get a list of users to present to the user.  This list may be a subset of the
 * available users and may be empty depending on the server configuration.
 *
 * Return value: A list of #LdmUser that should be presented to the user.
 **/
const GList *
ldm_greeter_get_users (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    update_users (greeter);
    return greeter->priv->users;
}

static void
update_languages (LdmGreeter *greeter)
{
    gchar *stdout_text = NULL, *stderr_text = NULL;
    gint exit_status;
    gboolean result;
    GError *error = NULL;

    if (greeter->priv->have_languages)
        return;

    result = g_spawn_command_line_sync ("locale -a", &stdout_text, &stderr_text, &exit_status, &error);
    if (!result || exit_status != 0)
        g_warning ("Failed to get languages, locale -a returned %d: %s", exit_status, error->message);
    else
    {
        gchar **tokens;
        int i;

        tokens = g_strsplit_set (stdout_text, "\n\r", -1);
        for (i = 0; tokens[i]; i++)
        {
            LdmLanguage *language;
            gchar *code;

            code = g_strchug (tokens[i]);
            if (code[0] == '\0')
                continue;

            /* Ignore the non-interesting languages */
            if (strcmp (code, "C") == 0 || strcmp (code, "POSIX") == 0)
                continue;

            language = ldm_language_new (code);
            greeter->priv->languages = g_list_append (greeter->priv->languages, language);
        }

        g_strfreev (tokens);
    }

    g_clear_error (&error);
    g_free (stdout_text);
    g_free (stderr_text);

    greeter->priv->have_languages = TRUE;
}

/**
 * ldm_greeter_get_default_language:
 * @greeter: A #LdmGreeter
 *
 * Get the default language.
 *
 * Return value: The default language.
 **/
const gchar *
ldm_greeter_get_default_language (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return getenv ("LANG");
}

/**
 * ldm_greeter_get_languages:
 * @greeter: A #LdmGreeter
 *
 * Get a list of languages to present to the user.
 *
 * Return value: A list of #LdmLanguage that should be presented to the user.
 **/
const GList *
ldm_greeter_get_languages (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    update_languages (greeter);
    return greeter->priv->languages;
}

const gchar *
ldm_greeter_get_default_layout (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->default_layout;
}

static void
layout_cb (XklConfigRegistry *config,
           const XklConfigItem *item,
           gpointer data)
{
    LdmGreeter *greeter = data;
    LdmLayout *layout;

    layout = ldm_layout_new (item->name, item->short_description, item->description);
    greeter->priv->layouts = g_list_append (greeter->priv->layouts, layout);
}

static void
setup_display (LdmGreeter *greeter)
{
    if (!greeter->priv->display)
        greeter->priv->display = XOpenDisplay (NULL);
}

static void
setup_xkl (LdmGreeter *greeter)
{
    setup_display (greeter);
    greeter->priv->xkl_engine = xkl_engine_get_instance (greeter->priv->display);
    greeter->priv->xkl_config = xkl_config_rec_new ();
    if (!xkl_config_rec_get_from_server (greeter->priv->xkl_config, greeter->priv->xkl_engine))
        g_warning ("Failed to get Xkl configuration from server");
    greeter->priv->layout = g_strdup (greeter->priv->xkl_config->layouts[0]);
}

/**
 * ldm_greeter_get_layouts:
 * @greeter: A #LdmGreeter
 *
 * Get a list of keyboard layouts to present to the user.
 *
 * Return value: A list of #LdmLayout that should be presented to the user.
 **/
const GList *
ldm_greeter_get_layouts (LdmGreeter *greeter)
{
    XklConfigRegistry *registry;

    g_return_val_if_fail (greeter != NULL, NULL);

    if (greeter->priv->have_layouts)
        return greeter->priv->layouts;

    setup_xkl (greeter);

    registry = xkl_config_registry_get_instance (greeter->priv->xkl_engine);
    xkl_config_registry_load (registry, FALSE);
    xkl_config_registry_foreach_layout (registry, layout_cb, greeter);
    g_object_unref (registry);
    greeter->priv->have_layouts = TRUE;

    return greeter->priv->layouts;
}

/**
 * ldm_greeter_set_layout:
 * @greeter: A #LdmGreeter
 * @layout: The layout to use
 *
 * Set the layout for this session.
 **/
void
ldm_greeter_set_layout (LdmGreeter *greeter, const gchar *layout)
{
    XklConfigRec *config;

    g_return_if_fail (LDM_IS_GREETER (greeter));
    g_return_if_fail (layout != NULL);

    g_debug ("Setting keyboard layout to %s", layout);

    setup_xkl (greeter);

    config = xkl_config_rec_new ();
    config->layouts = g_malloc (sizeof (gchar *) * 2);
    config->model = g_strdup (greeter->priv->xkl_config->model);
    config->layouts[0] = g_strdup (layout);
    config->layouts[1] = NULL;
    if (!xkl_config_rec_activate (config, greeter->priv->xkl_engine))
        g_warning ("Failed to activate XKL config");
    else
        greeter->priv->layout = g_strdup (layout);
    g_object_unref (config);
}

/**
 * ldm_greeter_get_layout:
 * @greeter: A #LdmGreeter
 *
 * Get the current keyboard layout.
 *
 * Return value: The currently active layout for this user.
 **/
const gchar *
ldm_greeter_get_layout (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    setup_xkl (greeter);
    return greeter->priv->layout;
}

static void
update_sessions (LdmGreeter *greeter)
{
    GDir *directory;
    GError *error = NULL;

    if (greeter->priv->have_sessions)
        return;

    directory = g_dir_open (XSESSIONS_DIR, 0, &error);
    if (!directory)
        g_warning ("Failed to open sessions directory: %s", error->message);
    g_clear_error (&error);
    if (!directory)
        return;

    while (TRUE)
    {
        const gchar *filename;
        GKeyFile *key_file;
        gchar *key, *path;
        gboolean result;

        filename = g_dir_read_name (directory);
        if (filename == NULL)
            break;

        if (!g_str_has_suffix (filename, ".desktop"))
            continue;

        key = g_strndup (filename, strlen (filename) - strlen (".desktop"));
        path = g_build_filename (XSESSIONS_DIR, filename, NULL);
        g_debug ("Loading session %s", path);

        key_file = g_key_file_new ();
        result = g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &error);
        if (!result)
            g_warning ("Failed to load session file %s: %s:", path, error->message);
        g_clear_error (&error);

        if (result && !g_key_file_get_boolean (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, NULL))
        {
            gchar *domain, *name, *comment;

#ifdef G_KEY_FILE_DESKTOP_KEY_GETTEXT_DOMAIN
            domain = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_GETTEXT_DOMAIN, NULL);
#else
            domain = g_key_file_get_string (key_file, G_KEY_FILE_DESKTOP_GROUP, "X-GNOME-Gettext-Domain", NULL);
#endif
            name = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, domain, NULL);
            comment = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_COMMENT, domain, NULL);
            if (!comment)
                comment = g_strdup ("");
            if (name)
            {
                g_debug ("Loaded session %s (%s, %s)", key, name, comment);
                greeter->priv->sessions = g_list_append (greeter->priv->sessions, ldm_session_new (key, name, comment));
            }
            else
                g_warning ("Invalid session %s: %s", path, error->message);
            g_clear_error (&error);
            g_free (domain);
            g_free (name);
            g_free (comment);
        }

        g_free (key);
        g_free (path);
        g_key_file_free (key_file);
    }

    g_dir_close (directory);

    greeter->priv->have_sessions = TRUE;
}

/**
 * ldm_greeter_get_sessions:
 * @greeter: A #LdmGreeter
 *
 * Get the available sessions.
 *
 * Return value: A list of #LdmSession
 **/
const GList *
ldm_greeter_get_sessions (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    update_sessions (greeter);
    return greeter->priv->sessions;
}

/**
 * ldm_greeter_get_default_session:
 * @greeter: A #LdmGreeter
 *
 * Get the default session to use.
 *
 * Return value: The session name
 **/
const gchar *
ldm_greeter_get_default_session (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->default_session;
}

/**
 * ldm_greeter_get_timed_login_user:
 * @greeter: A #LdmGreeter
 *
 * Get the user to log in by as default.
 *
 * Return value: A username
 */
const gchar *
ldm_greeter_get_timed_login_user (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->timed_user;
}

/**
 * ldm_greeter_get_timed_login_delay:
 * @greeter: A #LdmGreeter
 *
 * Get the number of seconds to wait until logging in as the default user.
 *
 * Return value: The number of seconds before logging in as the default user
 */
gint
ldm_greeter_get_timed_login_delay (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, 0);
    return greeter->priv->login_delay;
}

/**
 * ldm_greeter_cancel_timed_login:
 * @greeter: A #LdmGreeter
 *
 * Cancel the login as the default user.
 */
void
ldm_greeter_cancel_timed_login (LdmGreeter *greeter)
{
    g_return_if_fail (LDM_IS_GREETER (greeter));

    if (greeter->priv->login_timeout)
       g_source_remove (greeter->priv->login_timeout);
    greeter->priv->login_timeout = 0;
}

/**
 * ldm_greeter_start_authentication:
 * @greeter: A #LdmGreeter
 * @username: A username
 *
 * Starts the authentication procedure for a user.
 **/
void
ldm_greeter_start_authentication (LdmGreeter *greeter, const char *username)
{
    g_return_if_fail (LDM_IS_GREETER (greeter));
    g_return_if_fail (username != NULL);

    greeter->priv->is_authenticated = FALSE;
    g_free (greeter->priv->authentication_user);
    greeter->priv->authentication_user = g_strdup (username);
    g_debug ("Starting authentication for user %s...", username);
    write_int (greeter, GREETER_MESSAGE_START_AUTHENTICATION);
    write_string (greeter, username);
    flush (greeter);
}

/**
 * ldm_greeter_provide_secret:
 * @greeter: A #LdmGreeter
 * @secret: Response to a prompt
 *
 * Provide secret information from a prompt.
 **/
void
ldm_greeter_provide_secret (LdmGreeter *greeter, const gchar *secret)
{
    g_return_if_fail (LDM_IS_GREETER (greeter));
    g_return_if_fail (secret != NULL);

    g_debug ("Providing secret to display manager");
    write_int (greeter, GREETER_MESSAGE_CONTINUE_AUTHENTICATION);
    // FIXME: Could be multiple secrets required
    write_int (greeter, 1);
    write_string (greeter, secret);
    flush (greeter);
}

/**
 * ldm_greeter_cancel_authentication:
 * @greeter: A #LdmGreeter
 *
 * Cancel the current user authentication.
 **/
void
ldm_greeter_cancel_authentication (LdmGreeter *greeter)
{
    g_return_if_fail (LDM_IS_GREETER (greeter));
}

/**
 * ldm_greeter_get_is_authenticated:
 * @greeter: A #LdmGreeter
 *
 * Checks if the greeter has successfully authenticated.
 *
 * Return value: TRUE if the greeter is authenticated for login.
 **/
gboolean
ldm_greeter_get_is_authenticated (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, FALSE);
    return greeter->priv->is_authenticated;
}

/**
 * ldm_greeter_get_authentication_user:
 * @greeter: A #LdmGreeter
 *
 * Get the user that is being authenticated.
 *
 * Return value: The username of the authentication user being authenticated or NULL if no authentication in progress.
 */
const gchar *
ldm_greeter_get_authentication_user (LdmGreeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->authentication_user;
}

/**
 * ldm_greeter_login:
 * @greeter: A #LdmGreeter
 * @username: The user to log in as
 * @session: The session to log into or NULL to use the default
 * @language: The language to use or NULL to use the default
 *
 * Login a user to a session
 **/
void
ldm_greeter_login (LdmGreeter *greeter, const gchar *username, const gchar *session, const gchar *language)
{
    g_return_if_fail (LDM_IS_GREETER (greeter));
    g_return_if_fail (username != NULL);

    g_debug ("Logging in");
    write_int (greeter, GREETER_MESSAGE_LOGIN);
    write_string (greeter, username);
    write_string (greeter, session ? session : "");
    write_string (greeter, language ? language : "");
    flush (greeter);
}

static gboolean
upower_call_function (LdmGreeter *greeter, const gchar *function, gboolean has_result)
{
    GDBusProxy *proxy;
    GVariant *result;
    GError *error = NULL;
    gboolean function_result = FALSE;

    g_return_val_if_fail (greeter != NULL, FALSE);

    proxy = g_dbus_proxy_new_sync (greeter->priv->system_bus,
                                   G_DBUS_PROXY_FLAGS_NONE,
                                   NULL,
                                   "org.freedesktop.UPower",
                                   "/org/freedesktop/UPower",
                                   "org.freedesktop.UPower",
                                   NULL, NULL);
    result = g_dbus_proxy_call_sync (proxy,
                                     function,
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    g_object_unref (proxy);

    if (!result)
        g_warning ("Error calling UPower function %s: %s", function, error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE_BOOLEAN))
        function_result = g_variant_get_boolean (result);

    g_variant_unref (result);
    return function_result;
}

/**
 * ldm_greeter_get_can_suspend:
 * @greeter: A #LdmGreeter
 *
 * Checks if the greeter is authorized to do a system suspend.
 *
 * Return value: TRUE if the greeter can suspend the system
 **/
gboolean
ldm_greeter_get_can_suspend (LdmGreeter *greeter)
{
    return upower_call_function (greeter, "SuspendAllowed", TRUE);
}

/**
 * ldm_greeter_suspend:
 * @greeter: A #LdmGreeter
 *
 * Triggers a system suspend.
 **/
void
ldm_greeter_suspend (LdmGreeter *greeter)
{
    upower_call_function (greeter, "Suspend", FALSE);
}

/**
 * ldm_greeter_get_can_hibernate:
 * @greeter: A #LdmGreeter
 *
 * Checks if the greeter is authorized to do a system hibernate.
 *
 * Return value: TRUE if the greeter can hibernate the system
 **/
gboolean
ldm_greeter_get_can_hibernate (LdmGreeter *greeter)
{
    return upower_call_function (greeter, "HibernateAllowed", TRUE);
}

/**
 * ldm_greeter_hibernate:
 * @greeter: A #LdmGreeter
 *
 * Triggers a system hibernate.
 **/
void
ldm_greeter_hibernate (LdmGreeter *greeter)
{
    upower_call_function (greeter, "Hibernate", FALSE);
}

static gboolean
ck_call_function (LdmGreeter *greeter, const gchar *function, gboolean has_result)
{
    GDBusProxy *proxy;
    GVariant *result;
    GError *error = NULL;
    gboolean function_result = FALSE;

    g_return_val_if_fail (greeter != NULL, FALSE);

    proxy = g_dbus_proxy_new_sync (greeter->priv->system_bus,
                                   G_DBUS_PROXY_FLAGS_NONE,
                                   NULL,
                                   "org.freedesktop.ConsoleKit",
                                   "/org/freedesktop/ConsoleKit/Manager",
                                   "org.freedesktop.ConsoleKit.Manager",
                                   NULL, NULL);
    result = g_dbus_proxy_call_sync (proxy,
                                     function,
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);
    g_object_unref (proxy);

    if (!result)
        g_warning ("Error calling ConsoleKit function %s: %s", function, error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE_BOOLEAN))
        function_result = g_variant_get_boolean (result);

    g_variant_unref (result);
    return function_result;
}

/**
 * ldm_greeter_get_can_restart:
 * @greeter: A #LdmGreeter
 *
 * Checks if the greeter is authorized to do a system restart.
 *
 * Return value: TRUE if the greeter can restart the system
 **/
gboolean
ldm_greeter_get_can_restart (LdmGreeter *greeter)
{
    return ck_call_function (greeter, "CanRestart", TRUE);
}

/**
 * ldm_greeter_restart:
 * @greeter: A #LdmGreeter
 *
 * Triggers a system restart.
 **/
void
ldm_greeter_restart (LdmGreeter *greeter)
{
    ck_call_function (greeter, "Restart", FALSE);
}

/**
 * ldm_greeter_get_can_shutdown:
 * @greeter: A #LdmGreeter
 *
 * Checks if the greeter is authorized to do a system shutdown.
 *
 * Return value: TRUE if the greeter can shutdown the system
 **/
gboolean
ldm_greeter_get_can_shutdown (LdmGreeter *greeter)
{
    return ck_call_function (greeter, "CanStop", TRUE);
}

/**
 * ldm_greeter_shutdown:
 * @greeter: A #LdmGreeter
 *
 * Triggers a system shutdown.
 **/
void
ldm_greeter_shutdown (LdmGreeter *greeter)
{
    ck_call_function (greeter, "Stop", FALSE);
}

gboolean
ldm_greeter_get_user_defaults (LdmGreeter *greeter, const gchar *username, gchar **language, gchar **layout, gchar **session)
{
    GError *error = NULL;
    GVariant *result;
    gboolean got_defaults = FALSE;

    result = g_dbus_proxy_call_sync (greeter->priv->user_proxy,
                                     "GetUserDefaults",
                                     g_variant_new ("(s)", username),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);

    if (!result)
        g_warning ("Failed to get user defaults: %s", error->message);
    g_clear_error (&error);

    if (!result)
        return FALSE;

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(sss)")))
    {
        g_variant_get (result, "(sss)", language, layout, session);
        got_defaults = TRUE;
    }

    g_variant_unref (result);

    return got_defaults;
}

static void
ldm_greeter_init (LdmGreeter *greeter)
{
    greeter->priv = G_TYPE_INSTANCE_GET_PRIVATE (greeter, LDM_TYPE_GREETER, LdmGreeterPrivate);

    g_debug ("default-language=%s", ldm_greeter_get_default_language (greeter));
}

static void
ldm_greeter_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LdmGreeter *self;

    self = LDM_GREETER (object);

    switch (prop_id) {
    case PROP_LAYOUT:
        ldm_greeter_set_layout(self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ldm_greeter_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LdmGreeter *self;

    self = LDM_GREETER (object);

    switch (prop_id) {
    case PROP_HOSTNAME:
        g_value_set_string (value, ldm_greeter_get_hostname (self));
        break;
    case PROP_NUM_USERS:
        g_value_set_int (value, ldm_greeter_get_num_users (self));
        break;
    case PROP_USERS:
        break;
    case PROP_DEFAULT_LANGUAGE:
        g_value_set_string (value, ldm_greeter_get_default_language (self));
        break;
    case PROP_LAYOUTS:
        break;
    case PROP_LAYOUT:
        g_value_set_string (value, ldm_greeter_get_layout (self));
        break;
    case PROP_SESSIONS:
        break;
    case PROP_DEFAULT_SESSION:
        g_value_set_string (value, ldm_greeter_get_default_session (self));
        break;
    case PROP_TIMED_LOGIN_USER:
        g_value_set_string (value, ldm_greeter_get_timed_login_user (self));
        break;
    case PROP_TIMED_LOGIN_DELAY:
        g_value_set_int (value, ldm_greeter_get_timed_login_delay (self));
        break;
    case PROP_AUTHENTICATION_USER:
        g_value_set_string (value, ldm_greeter_get_authentication_user (self));
        break;
    case PROP_IS_AUTHENTICATED:
        g_value_set_boolean (value, ldm_greeter_get_is_authenticated (self));
        break;
    case PROP_CAN_SUSPEND:
        g_value_set_boolean (value, ldm_greeter_get_can_suspend (self));
        break;
    case PROP_CAN_HIBERNATE:
        g_value_set_boolean (value, ldm_greeter_get_can_hibernate (self));
        break;
    case PROP_CAN_RESTART:
        g_value_set_boolean (value, ldm_greeter_get_can_restart (self));
        break;
    case PROP_CAN_SHUTDOWN:
        g_value_set_boolean (value, ldm_greeter_get_can_shutdown (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ldm_greeter_class_init (LdmGreeterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (LdmGreeterPrivate));

    object_class->set_property = ldm_greeter_set_property;
    object_class->get_property = ldm_greeter_get_property;

    g_object_class_install_property (object_class,
                                     PROP_NUM_USERS,
                                     g_param_spec_string ("hostname",
                                                          "hostname",
                                                          "Hostname displaying greeter for",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_NUM_USERS,
                                     g_param_spec_int ("num-users",
                                                       "num-users",
                                                       "Number of login users",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READABLE));
    /*g_object_class_install_property (object_class,
                                     PROP_USERS,
                                     g_param_spec_list ("users",
                                                        "users",
                                                        "Users that can login"));
    g_object_class_install_property (object_class,
                                     PROP_DEFAULT_LANGUAGE,
                                     g_param_spec_string ("default-language",
                                                          "default-language",
                                                          "Default language",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     PROP_LAYOUTS,
                                     g_param_spec_list ("layouts",
                                                        "layouts",
                                                        "Available keyboard layouts"));*/
    g_object_class_install_property (object_class,
                                     PROP_LAYOUT,
                                     g_param_spec_string ("layout",
                                                          "layout",
                                                          "Current keyboard layout",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    /*g_object_class_install_property (object_class,
                                     PROP_SESSIONS,
                                     g_param_spec_list ("sessions",
                                                        "sessions",
                                                        "Available sessions"));*/
    g_object_class_install_property (object_class,
                                     PROP_DEFAULT_SESSION,
                                     g_param_spec_string ("default-session",
                                                          "default-session",
                                                          "Default session",
                                                          NULL,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     PROP_TIMED_LOGIN_USER,
                                     g_param_spec_string ("timed-login-user",
                                                          "timed-login-user",
                                                          "User to login as when timed expires",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_TIMED_LOGIN_DELAY,
                                     g_param_spec_int ("login-delay",
                                                       "login-delay",
                                                       "Number of seconds until logging in as default user",
                                                       G_MININT, G_MAXINT, 0,
                                                       G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_AUTHENTICATION_USER,
                                     g_param_spec_string ("authentication-user",
                                                          "authentication-user",
                                                          "The user being authenticated",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_IS_AUTHENTICATED,
                                     g_param_spec_boolean ("is-authenticated",
                                                           "is-authenticated",
                                                           "TRUE if the selected user is authenticated",
                                                           FALSE,
                                                           G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_CAN_SUSPEND,
                                     g_param_spec_boolean ("can-suspend",
                                                           "can-suspend",
                                                           "TRUE if allowed to suspend the system",
                                                           FALSE,
                                                           G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_CAN_HIBERNATE,
                                     g_param_spec_boolean ("can-hibernate",
                                                           "can-hibernate",
                                                           "TRUE if allowed to hibernate the system",
                                                           FALSE,
                                                           G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_CAN_RESTART,
                                     g_param_spec_boolean ("can-restart",
                                                           "can-restart",
                                                           "TRUE if allowed to restart the system",
                                                           FALSE,
                                                           G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_CAN_SHUTDOWN,
                                     g_param_spec_boolean ("can-shutdown",
                                                           "can-shutdown",
                                                           "TRUE if allowed to shutdown the system",
                                                           FALSE,
                                                           G_PARAM_READABLE));

    /**
     * LdmGreeter::connected:
     * @greeter: A #LdmGreeter
     *
     * The ::connected signal gets emitted when the greeter connects to the
     * LightDM server.
     **/
    signals[CONNECTED] =
        g_signal_new ("connected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, connected),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    /**
     * LdmGreeter::show-prompt:
     * @greeter: A #LdmGreeter
     * @text: Prompt text
     *
     * The ::show-prompt signal gets emitted when the greeter should show a
     * prompt to the user.  The given text should be displayed and an input
     * field for the user to provide a response.
     *
     * Call ldm_greeter_provide_secret() with the resultant input or
     * ldm_greeter_cancel_authentication() to abort the authentication.
     **/
    signals[SHOW_PROMPT] =
        g_signal_new ("show-prompt",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, show_prompt),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * LdmGreeter::show-message:
     * @greeter: A #LdmGreeter
     * @text: Message text
     *
     * The ::show-message signal gets emitted when the greeter
     * should show an informational message to the user.
     **/
    signals[SHOW_MESSAGE] =
        g_signal_new ("show-message",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, show_message),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * LdmGreeter::show-error:
     * @greeter: A #LdmGreeter
     * @text: Message text
     *
     * The ::show-error signal gets emitted when the greeter
     * should show an error message to the user.
     **/
    signals[SHOW_ERROR] =
        g_signal_new ("show-error",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, show_error),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * LdmGreeter::authentication-complete:
     * @greeter: A #LdmGreeter
     *
     * The ::authentication-complete signal gets emitted when the greeter
     * has completed authentication.
     *
     * Call ldm_greeter_get_is_authenticated() to check if the authentication
     * was successful.
     **/
    signals[AUTHENTICATION_COMPLETE] =
        g_signal_new ("authentication-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, authentication_complete),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    /**
     * LdmGreeter::timed-login:
     * @greeter: A #LdmGreeter
     * @username: A username
     *
     * The ::timed-login signal gets emitted when the default user timer
     * has expired.
     **/
    signals[TIMED_LOGIN] =
        g_signal_new ("timed-login",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, timed_login),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * LdmGreeter::quit:
     * @greeter: A #LdmGreeter
     *
     * The ::quit signal gets emitted when the greeter should exit.
     **/
    signals[QUIT] =
        g_signal_new ("quit",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, quit),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
