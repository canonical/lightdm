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
#include <locale.h>
#include <sys/utsname.h>

#include <gio/gdesktopappinfo.h>
#include <dbus/dbus-glib.h>
#include <security/pam_appl.h>
#include <libxklavier/xklavier.h>

#include "greeter.h"

enum {
    PROP_0,
    PROP_HOSTNAME,  
    PROP_NUM_USERS,
    PROP_USERS,
    PROP_LAYOUTS,
    PROP_LAYOUT,
    PROP_SESSIONS,
    PROP_SESSION,
    PROP_TIMED_LOGIN_USER,
    PROP_TIMED_LOGIN_DELAY,
    PROP_IS_AUTHENTICATED,
    PROP_CAN_SUSPEND,
    PROP_CAN_HIBERNATE,
    PROP_CAN_RESTART,
    PROP_CAN_SHUTDOWN
};

enum {
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
    DBusGConnection *lightdm_bus;

    DBusGConnection *system_bus;

    DBusGProxy *display_proxy, *session_proxy, *user_proxy;

    Display *display;

    gchar *hostname;

    gchar *theme;
    GKeyFile *theme_file;

    gboolean have_users;
    GList *users;
  
    gboolean have_languages;
    GList *languages;

    XklEngine *xkl_engine;
    XklConfigRec *xkl_config;
    gboolean have_layouts;
    GList *layouts;
    gchar *layout;

    gboolean have_sessions;
    GList *sessions;
    gchar *session;

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

    g_signal_emit (G_OBJECT (greeter), signals[TIMED_LOGIN], 0, greeter->priv->timed_user);

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
    const gchar *bus_address, *object;
    DBusBusType bus_type = DBUS_BUS_SYSTEM;
    gboolean result;

    greeter->priv->system_bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
    if (!greeter->priv->system_bus)
        g_warning ("Failed to connect to system bus: %s", error->message);
    g_clear_error (&error);
    if (!greeter->priv->system_bus)
        return FALSE;

    bus_address = getenv ("LDM_BUS");
    if (bus_address && strcmp (bus_address, "SESSION") == 0)
        bus_type = DBUS_BUS_SESSION;

    greeter->priv->lightdm_bus = dbus_g_bus_get (bus_type, &error);
    if (!greeter->priv->lightdm_bus)
        g_warning ("Failed to connect to LightDM bus: %s", error->message);
    g_clear_error (&error);
    if (!greeter->priv->lightdm_bus)
        return FALSE;

    object = getenv ("LDM_DISPLAY");
    if (!object)
    {
        g_warning ("No LDM_DISPLAY enviroment variable");
        return FALSE;
    }

    greeter->priv->display_proxy = dbus_g_proxy_new_for_name (greeter->priv->lightdm_bus,
                                                              "org.gnome.LightDisplayManager",
                                                              object,
                                                              "org.gnome.LightDisplayManager.Greeter");
    greeter->priv->session_proxy = dbus_g_proxy_new_for_name (greeter->priv->lightdm_bus,
                                                              "org.gnome.LightDisplayManager",
                                                              "/org/gnome/LightDisplayManager/Session",
                                                              "org.gnome.LightDisplayManager.Session");
    greeter->priv->user_proxy = dbus_g_proxy_new_for_name (greeter->priv->lightdm_bus,
                                                           "org.gnome.LightDisplayManager",
                                                           "/org/gnome/LightDisplayManager/Users",
                                                           "org.gnome.LightDisplayManager.Users");

    result = dbus_g_proxy_call (greeter->priv->display_proxy, "Connect", &error,
                                G_TYPE_INVALID,
                                G_TYPE_STRING, &greeter->priv->theme,
                                G_TYPE_STRING, &greeter->priv->session,
                                G_TYPE_STRING, &greeter->priv->timed_user,
                                G_TYPE_INT, &greeter->priv->login_delay,
                                G_TYPE_INVALID);

    if (!result)
        g_warning ("Failed to connect to display manager: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return FALSE;

    /* Set timeout for default login */
    if (greeter->priv->timed_user[0] != '\0' && greeter->priv->login_delay > 0)
    {
        g_debug ("Logging in as %s in %d seconds", greeter->priv->timed_user, greeter->priv->login_delay);
        greeter->priv->login_timeout = g_timeout_add (greeter->priv->login_delay * 1000, timed_login_cb, greeter);
    }

    return result;
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

    load_theme (greeter);

    result = g_key_file_get_boolean (greeter->priv->theme_file, "theme", name, &error);
    if (!result)
        g_warning ("Error reading theme property: %s", error->message); // FIXME: Can handle G_KEY_FILE_ERROR_KEY_NOT_FOUND and G_KEY_FILE_ERROR_GROUP_NOT_FOUND
    g_clear_error (&error);

    return result;
}

#define TYPE_USER dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID)
#define TYPE_USER_LIST dbus_g_type_get_collection ("GPtrArray", TYPE_USER)

static void
update_users (LdmGreeter *greeter)
{
    GPtrArray *users;
    gboolean result;
    gint i;
    GError *error = NULL;

    if (greeter->priv->have_users)
        return;

    result = dbus_g_proxy_call (greeter->priv->user_proxy, "GetUsers", &error,
                                G_TYPE_INVALID,
                                TYPE_USER_LIST, &users,
                                G_TYPE_INVALID);
    if (!result)
        g_warning ("Failed to get users: %s", error->message);
    g_clear_error (&error);
  
    if (!result)
        return;
  
    for (i = 0; i < users->len; i++)
    {
        GValue value = { 0 };
        LdmUser *user;
        gchar *name, *real_name, *image;
        gboolean logged_in;

        g_value_init (&value, TYPE_USER);
        g_value_set_static_boxed (&value, users->pdata[i]);
        dbus_g_type_struct_get (&value, 0, &name, 1, &real_name, 2, &image, 3, &logged_in, G_MAXUINT);
        g_value_unset (&value);

        user = ldm_user_new (name, real_name, image, logged_in);
        g_free (name);
        g_free (real_name);
        g_free (image);

        greeter->priv->users = g_list_append (greeter->priv->users, user);
    }

    g_ptr_array_free (users, TRUE);

    greeter->priv->have_users = TRUE;
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
    update_languages (greeter);
    return greeter->priv->languages;
}

/**
 * ldm_greeter_get_language:
 * @greeter: A #LdmGreeter
 * 
 * Get the current language.
 * 
 * Return value: The current language.
 **/
const gchar *
ldm_greeter_get_language (LdmGreeter *greeter)
{
    return setlocale (LC_ALL, NULL);
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
    setup_xkl (greeter);
    return greeter->priv->layout;
}

static void
update_sessions (LdmGreeter *greeter)
{
    GDir *directory;
    GError *error = NULL;
    GKeyFile *key_file;

    if (greeter->priv->have_sessions)
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

        result = g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &error);
        if (!result)
            g_warning ("Failed to load session file %s: %s:", path, error->message);
        g_clear_error (&error);

        if (result)
        {
            GDesktopAppInfo *desktop_file;

            desktop_file = g_desktop_app_info_new_from_keyfile (key_file);

            if (desktop_file && g_app_info_should_show (G_APP_INFO (desktop_file)))
            {
                const gchar *name, *comment;

                name = g_app_info_get_name (G_APP_INFO (desktop_file));
                comment = g_app_info_get_display_name (G_APP_INFO (desktop_file));
                if (name && comment)
                {
                    g_debug ("Loaded session %s (%s, %s)", key, name, comment);
                    greeter->priv->sessions = g_list_append (greeter->priv->sessions, ldm_session_new (key, name, comment));
                }
                else
                    g_warning ("Invalid session %s: %s", path, error->message);
                g_clear_error (&error);
            }

            if (desktop_file)
                g_object_unref (desktop_file);
        }

        g_free (key);
        g_free (path);
    }

    g_dir_close (directory);
    g_key_file_free (key_file);

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
    update_sessions (greeter);
    return greeter->priv->sessions;
}

/**
 * ldm_greeter_set_session:
 * @greeter: A #LdmGreeter
 * @session: A session name.
 * 
 * Set the session to log into.
 **/
void
ldm_greeter_set_session (LdmGreeter *greeter, const gchar *session)
{
    GError *error = NULL;

    if (!dbus_g_proxy_call (greeter->priv->display_proxy, "SetSession", &error,
                            G_TYPE_STRING, session,
                            G_TYPE_INVALID,
                            G_TYPE_INVALID))
        g_warning ("Failed to set session: %s", error->message);
    else
    {
        g_free (greeter->priv->session);
        greeter->priv->session = g_strdup (session);
    }
    g_clear_error (&error);
}

/**
 * ldm_greeter_get_session:
 * @greeter: A #LdmGreeter
 *
 * Get the session that will be logged into.
 *
 * Return value: The session name
 **/
const gchar *
ldm_greeter_get_session (LdmGreeter *greeter)
{
    return greeter->priv->session;
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
    if (greeter->priv->login_timeout)
       g_source_remove (greeter->priv->login_timeout);
    greeter->priv->login_timeout = 0;
}

#define TYPE_MESSAGE dbus_g_type_get_struct ("GValueArray", G_TYPE_INT, G_TYPE_STRING, G_TYPE_INVALID)
#define TYPE_MESSAGE_LIST dbus_g_type_get_collection ("GPtrArray", TYPE_MESSAGE)

static void
auth_response_cb (DBusGProxy *proxy, DBusGProxyCall *call, gpointer userdata)
{
    LdmGreeter *greeter = userdata;
    gboolean result;
    GError *error = NULL;
    gint return_code;
    GPtrArray *array;
    int i;

    result = dbus_g_proxy_end_call (proxy, call, &error, G_TYPE_INT, &return_code, TYPE_MESSAGE_LIST, &array, G_TYPE_INVALID);
    if (!result)
        g_warning ("Failed to complete D-Bus call: %s", error->message);
    g_clear_error (&error);
    if (!result)
        return;

    for (i = 0; i < array->len; i++)
    {
        GValue value = { 0 };
        gint msg_style;
        gchar *msg;
      
        g_value_init (&value, TYPE_MESSAGE);
        g_value_set_static_boxed (&value, array->pdata[i]);
        dbus_g_type_struct_get (&value, 0, &msg_style, 1, &msg, G_MAXUINT);

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

        g_value_unset (&value);
    }

    if (array->len == 0)
    {
        greeter->priv->is_authenticated = (return_code == 0);
        g_signal_emit (G_OBJECT (greeter), signals[AUTHENTICATION_COMPLETE], 0);
    }

    g_ptr_array_unref (array);
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
    dbus_g_proxy_begin_call (greeter->priv->display_proxy, "StartAuthentication", auth_response_cb, greeter, NULL, G_TYPE_STRING, username, G_TYPE_INVALID);
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
    gchar **secrets;

    // FIXME: Could be multiple secrets required
    secrets = g_malloc (sizeof (char *) * 2);
    secrets[0] = g_strdup (secret);
    secrets[1] = NULL;
    dbus_g_proxy_begin_call (greeter->priv->display_proxy, "ContinueAuthentication", auth_response_cb, greeter, NULL, G_TYPE_STRV, secrets, G_TYPE_INVALID);
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
    return greeter->priv->is_authenticated;
}

/**
 * ldm_greeter_login:
 * @greeter: A #LdmGreeter
 * 
 * Login with the currently authenticated user.
 **/
void
ldm_greeter_login (LdmGreeter *greeter)
{
    /* Quitting the greeter will cause the login to occur */
    g_signal_emit (G_OBJECT (greeter), signals[QUIT], 0);
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
    DBusGProxy *proxy;
    gboolean result = TRUE;
    GError *error = NULL;

    proxy = dbus_g_proxy_new_for_name (greeter->priv->system_bus,
                                       "org.freedesktop.UPower",
                                       "/org/freedesktop/UPower",
                                       "org.freedesktop.UPower");
    if (!dbus_g_proxy_call (proxy, "SuspendAllowed", &error, G_TYPE_INVALID, G_TYPE_BOOLEAN, &result, G_TYPE_INVALID))
        g_warning ("Error checking for suspend authority: %s", error->message);
    g_clear_error (&error);

    g_object_unref (proxy);

    return result;
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
    DBusGProxy *proxy;
    GError *error = NULL;

    proxy = dbus_g_proxy_new_for_name (greeter->priv->system_bus,
                                       "org.freedesktop.UPower",
                                       "/org/freedesktop/UPower",
                                       "org.freedesktop.UPower");
    if (!dbus_g_proxy_call (proxy, "Suspend", &error, G_TYPE_INVALID, G_TYPE_INVALID))
        g_warning ("Failed to hibernate: %s", error->message);
    g_clear_error (&error);

    g_object_unref (proxy);
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
    DBusGProxy *proxy;
    gboolean result = FALSE;
    GError *error = NULL;

    proxy = dbus_g_proxy_new_for_name (greeter->priv->system_bus,
                                       "org.freedesktop.UPower",
                                       "/org/freedesktop/UPower",
                                       "org.freedesktop.UPower");
    if (!dbus_g_proxy_call (proxy, "HibernateAllowed", &error, G_TYPE_INVALID, G_TYPE_BOOLEAN, &result, G_TYPE_INVALID))
        g_warning ("Error checking for hibernate authority: %s", error->message);
    g_clear_error (&error);

    g_object_unref (proxy);

    return result;
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
    DBusGProxy *proxy;
    GError *error = NULL;

    proxy = dbus_g_proxy_new_for_name (greeter->priv->system_bus,
                                       "org.freedesktop.UPower",
                                       "/org/freedesktop/UPower",
                                       "org.freedesktop.UPower");
    if (!dbus_g_proxy_call (proxy, "Hibernate", &error, G_TYPE_INVALID, G_TYPE_INVALID))
        g_warning ("Failed to hibernate: %s", error->message);
    g_clear_error (&error);

    g_object_unref (proxy);
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
    DBusGProxy *proxy;
    gboolean result = FALSE;
    GError *error = NULL;

    proxy = dbus_g_proxy_new_for_name (greeter->priv->system_bus,
                                       "org.freedesktop.ConsoleKit",
                                       "/org/freedesktop/ConsoleKit/Manager",
                                       "org.freedesktop.ConsoleKit.Manager");
    if (!dbus_g_proxy_call (proxy, "CanRestart", &error, G_TYPE_INVALID, G_TYPE_BOOLEAN, &result, G_TYPE_INVALID))
        g_warning ("Error checking for restart authority: %s", error->message);
    g_clear_error (&error);

    g_object_unref (proxy);

    return result; 
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
    DBusGProxy *proxy;
    GError *error = NULL;

    proxy = dbus_g_proxy_new_for_name (greeter->priv->system_bus,
                                       "org.freedesktop.ConsoleKit",
                                       "/org/freedesktop/ConsoleKit/Manager",
                                       "org.freedesktop.ConsoleKit.Manager");
    if (!dbus_g_proxy_call (proxy, "Restart", &error, G_TYPE_INVALID, G_TYPE_INVALID))
        g_warning ("Failed to restart: %s", error->message);
    g_clear_error (&error);

    g_object_unref (proxy);
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
    DBusGProxy *proxy;
    gboolean result = FALSE;
    GError *error = NULL;

    proxy = dbus_g_proxy_new_for_name (greeter->priv->system_bus,
                                       "org.freedesktop.ConsoleKit",
                                       "/org/freedesktop/ConsoleKit/Manager",
                                       "org.freedesktop.ConsoleKit.Manager");
    if (!dbus_g_proxy_call (proxy, "CanStop", &error, G_TYPE_INVALID, G_TYPE_BOOLEAN, &result, G_TYPE_INVALID))
        g_warning ("Error checking for shutdown authority: %s", error->message);
    g_clear_error (&error);

    g_object_unref (proxy);

    return result; 
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
    DBusGProxy *proxy;
    GError *error = NULL;

    proxy = dbus_g_proxy_new_for_name (greeter->priv->system_bus,
                                       "org.freedesktop.ConsoleKit",
                                       "/org/freedesktop/ConsoleKit/Manager",
                                       "org.freedesktop.ConsoleKit.Manager");
    if (!dbus_g_proxy_call (proxy, "Stop", &error, G_TYPE_INVALID, G_TYPE_INVALID))
        g_warning ("Failed to shutdown: %s", error->message);
    g_clear_error (&error);

    g_object_unref (proxy);
}

static void
ldm_greeter_init (LdmGreeter *greeter)
{
    greeter->priv = G_TYPE_INSTANCE_GET_PRIVATE (greeter, LDM_TYPE_GREETER, LdmGreeterPrivate);
}

static void
ldm_greeter_set_property(GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    LdmGreeter *self;
    gint i, n_pages;

    self = LDM_GREETER (object);

    switch (prop_id) {
    case PROP_LAYOUT:
        ldm_greeter_set_layout(self, g_value_get_string (value));
        break;
    case PROP_SESSION:
        ldm_greeter_set_session(self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
ldm_greeter_get_property(GObject    *object,
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
    case PROP_LAYOUTS:
        break;
    case PROP_LAYOUT:
        g_value_set_string (value, ldm_greeter_get_layout (self));
        break;      
    case PROP_SESSIONS:
        break;
    case PROP_SESSION:
        g_value_set_string (value, ldm_greeter_get_session (self));
        break;
    case PROP_TIMED_LOGIN_USER:
        g_value_set_string (value, ldm_greeter_get_timed_login_user (self));
        break;
    case PROP_TIMED_LOGIN_DELAY:
        g_value_set_int (value, ldm_greeter_get_timed_login_delay (self));
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
                                     PROP_SESSION,
                                     g_param_spec_string ("session",
                                                          "session",
                                                          "Selected session",
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
