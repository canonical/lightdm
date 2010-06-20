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

#include <dbus/dbus-glib.h>
#include <security/pam_appl.h>

#include "greeter.h"

enum {
    PROP_0,
    PROP_NUM_USERS,
    PROP_USERS,
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
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct LdmGreeterPrivate
{
    DBusGConnection *lightdm_bus;

    DBusGConnection *system_bus;

    DBusGProxy *display_proxy, *session_proxy, *user_proxy;

    gboolean have_users;
    GList *users;

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
    gboolean result;
    GError *error = NULL;

    result = dbus_g_proxy_call (greeter->priv->display_proxy, "Connect", &error,
                                G_TYPE_INVALID,
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
 * @greeter:
 * 
 * Return value: A list of #LdmUser that should be presented to the user.
 */
const GList *
ldm_greeter_get_users (LdmGreeter *greeter)
{
    update_users (greeter);
    return greeter->priv->users;
}

#define TYPE_SESSION dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID)
#define TYPE_SESSION_LIST dbus_g_type_get_collection ("GPtrArray", TYPE_SESSION)

static void
update_sessions (LdmGreeter *greeter)
{
    GPtrArray *sessions;
    gboolean result;
    gint i;
    GError *error = NULL;

    if (greeter->priv->have_sessions)
        return;

    result = dbus_g_proxy_call (greeter->priv->session_proxy, "GetSessions", &error,
                                G_TYPE_INVALID,
                                TYPE_SESSION_LIST, &sessions,
                                G_TYPE_INVALID);
    if (!result)
        g_warning ("Failed to get sessions: %s", error->message);
    g_clear_error (&error);
  
    if (!result)
        return;
  
    for (i = 0; i < sessions->len; i++)
    {
        GValue value = { 0 };
        LdmSession *session;
        gchar *key, *name, *comment;

        g_value_init (&value, TYPE_SESSION);
        g_value_set_static_boxed (&value, sessions->pdata[i]);
        dbus_g_type_struct_get (&value, 0, &key, 1, &name, 2, &comment, G_MAXUINT);
        g_value_unset (&value);

        session = ldm_session_new (key, name, comment);
        g_free (key);
        g_free (name);
        g_free (comment);

        greeter->priv->sessions = g_list_append (greeter->priv->sessions, session);
    }

    g_ptr_array_free (sessions, TRUE);

    greeter->priv->have_sessions = TRUE;
}

const GList *
ldm_greeter_get_sessions (LdmGreeter *greeter)
{
    update_sessions (greeter);
    return greeter->priv->sessions;
}

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

const gchar *
ldm_greeter_get_session (LdmGreeter *greeter)
{
    return greeter->priv->session;
}

const gchar *
ldm_greeter_get_timed_login_user (LdmGreeter *greeter)
{
    return greeter->priv->timed_user;
}

gint
ldm_greeter_get_timed_login_delay (LdmGreeter *greeter)
{
    return greeter->priv->login_delay;
}

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

void
ldm_greeter_start_authentication (LdmGreeter *greeter, const char *username)
{
    dbus_g_proxy_begin_call (greeter->priv->display_proxy, "StartAuthentication", auth_response_cb, greeter, NULL, G_TYPE_STRING, username, G_TYPE_INVALID);
}

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

void
ldm_greeter_cancel_authentication (LdmGreeter *greeter)
{
}

gboolean
ldm_greeter_get_is_authenticated (LdmGreeter *greeter)
{
    return greeter->priv->is_authenticated;
}

/**
 * ldm_greeter_get_can_suspend:
 * @greeter: A #LdmGreeter
 *
 * Return value: TRUE if the greeter can suspend the machine
 **/
gboolean
ldm_greeter_get_can_suspend (LdmGreeter *greeter)
{
    DBusGProxy *proxy;
    gboolean result = FALSE;
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
    GError *error = NULL;
    const gchar *bus_address, *object;
    DBusBusType bus_type = DBUS_BUS_SYSTEM;

    greeter->priv = G_TYPE_INSTANCE_GET_PRIVATE (greeter, LDM_TYPE_GREETER, LdmGreeterPrivate);

    greeter->priv->system_bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
    if (!greeter->priv->system_bus)
        g_error ("Failed to connect to system bus: %s", error->message);
    g_clear_error (&error);

    bus_address = getenv ("LDM_BUS");
    if (bus_address && strcmp (bus_address, "SESSION") == 0)
        bus_type = DBUS_BUS_SESSION;

    greeter->priv->lightdm_bus = dbus_g_bus_get (bus_type, &error);
    if (!greeter->priv->lightdm_bus)
        g_error ("Failed to connect to LightDM bus: %s", error->message);
    g_clear_error (&error);

    object = getenv ("LDM_DISPLAY");
    if (!object)
        g_error ("No LDM_DISPLAY enviroment variable");

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
    case PROP_NUM_USERS:
        g_value_set_int (value, ldm_greeter_get_num_users (self));
        break;
    case PROP_USERS:
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

    g_object_class_install_property(object_class,
                                    PROP_NUM_USERS,
                                    g_param_spec_int("num-users",
                                                     "num- users",
                                                     "Number of login users",
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READABLE));
    /*g_object_class_install_property(object_class,
                                    PROP_USERS,
                                    g_param_spec_list("users",
                                                      "users",
                                                      "Users that can login"));
    g_object_class_install_property(object_class,
                                    PROP_SESSIONS,
                                    g_param_spec_list("sessions",
                                                      "sessions",
                                                      "Available sessions"));*/
    g_object_class_install_property(object_class,
                                    PROP_SESSION,
                                    g_param_spec_string("session",
                                                        "session",
                                                        "Selected session",
                                                        NULL,
                                                        G_PARAM_READWRITE));
    g_object_class_install_property(object_class,
                                    PROP_TIMED_LOGIN_USER,
                                    g_param_spec_string("timed-login-user",
                                                        "timed-login-user",
                                                        "User to login as when timed expires",
                                                        NULL,
                                                        G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_TIMED_LOGIN_DELAY,
                                    g_param_spec_int("login-delay",
                                                     "login-delay",
                                                     "Number of seconds until logging in as default user",
                                                     G_MININT, G_MAXINT, 0,
                                                     G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_IS_AUTHENTICATED,
                                    g_param_spec_boolean("is-authenticated",
                                                         "is-authenticated",
                                                         "TRUE if the selected user is authenticated",
                                                         FALSE,
                                                         G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_CAN_SUSPEND,
                                    g_param_spec_boolean("can-suspend",
                                                         "can-suspend",
                                                         "TRUE if allowed to suspend the machine",
                                                         FALSE,
                                                         G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_CAN_HIBERNATE,
                                    g_param_spec_boolean("can-hibernate",
                                                         "can-hibernate",
                                                         "TRUE if allowed to hibernate the machine",
                                                         FALSE,
                                                         G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_CAN_RESTART,
                                    g_param_spec_boolean("can-restart",
                                                         "can-restart",
                                                         "TRUE if allowed to restart the machine",
                                                         FALSE,
                                                         G_PARAM_READABLE));
    g_object_class_install_property(object_class,
                                    PROP_CAN_SHUTDOWN,
                                    g_param_spec_boolean("can-shutdown",
                                                         "can-shutdown",
                                                         "TRUE if allowed to shutdown the machine",
                                                         FALSE,
                                                         G_PARAM_READABLE));

    /**
     * LdmGreeter::show-prompt:
     * @greeter: The greeter on which the signal is emitted
     * @text: The text to show in the prompt
     * 
     * The ::show-prompt signal gets emitted when the greeter
     * should show a prompt to the user.
     **/
    signals[SHOW_PROMPT] =
        g_signal_new ("show-prompt",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, show_prompt),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
    signals[SHOW_MESSAGE] =
        g_signal_new ("show-message",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, show_message),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
    signals[SHOW_ERROR] =
        g_signal_new ("show-error",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, show_error),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
    signals[AUTHENTICATION_COMPLETE] =
        g_signal_new ("authentication-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, authentication_complete),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
    signals[TIMED_LOGIN] =
        g_signal_new ("timed-login",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LdmGreeterClass, timed_login),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
}
