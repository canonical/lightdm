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

#include <config.h>

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <locale.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <gio/gdesktopappinfo.h>
#include <security/pam_appl.h>
#include <libxklavier/xklavier.h>

#include "lightdm/greeter.h"
#include "user-private.h"

enum {
    PROP_0,
    PROP_HOSTNAME,
    PROP_NUM_USERS,
    PROP_USERS,
    PROP_DEFAULT_LANGUAGE,
    PROP_LAYOUTS,
    PROP_LAYOUT,
    PROP_SESSIONS,
    PROP_DEFAULT_SESSION_HINT,
    PROP_HIDE_USERS_HINT,
    PROP_HAS_GUEST_ACCOUNT_HINT,
    PROP_SELECT_USER_HINT,
    PROP_SELECT_GUEST_HINT,
    PROP_AUTOLOGIN_USER_HINT,
    PROP_AUTOLOGIN_GUEST_HINT,
    PROP_AUTOLOGIN_TIMEOUT_HINT,
    PROP_AUTHENTICATION_USER,
    PROP_IN_AUTHENTICATION,
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
    AUTHENTICATION_COMPLETE,
    SESSION_FAILED,
    AUTOLOGIN_TIMER_EXPIRED,
    USER_ADDED,
    USER_CHANGED,
    USER_REMOVED,
    QUIT,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    GDBusConnection *lightdm_bus;

    GDBusConnection *system_bus;

    GIOChannel *to_server_channel, *from_server_channel;
    guint8 *read_buffer;
    gsize n_read;

    Display *display;

    gchar *hostname;

    /* File monitor for password file */
    GFileMonitor *passwd_monitor;
  
    /* TRUE if have scanned users */
    gboolean have_users;

    /* List of users */
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

    gchar *authentication_user;
    gboolean in_authentication;
    gboolean is_authenticated;
    guint32 authenticate_sequence_number;
    gboolean cancelling_authentication;
  
    GHashTable *hints;

    guint login_timeout;
} LightDMGreeterPrivate;

G_DEFINE_TYPE (LightDMGreeter, lightdm_greeter, G_TYPE_OBJECT);

#define GET_PRIVATE(obj) G_TYPE_INSTANCE_GET_PRIVATE ((obj), LIGHTDM_TYPE_GREETER, LightDMGreeterPrivate)

#define HEADER_SIZE 8
#define MAX_MESSAGE_LENGTH 1024

#define PASSWD_FILE      "/etc/passwd"
#define USER_CONFIG_FILE "/etc/lightdm/users.conf"

/* Messages from the greeter to the server */
typedef enum
{
    GREETER_MESSAGE_CONNECT = 0,
    GREETER_MESSAGE_LOGIN,
    GREETER_MESSAGE_LOGIN_AS_GUEST,
    GREETER_MESSAGE_CONTINUE_AUTHENTICATION,
    GREETER_MESSAGE_START_SESSION,
    GREETER_MESSAGE_CANCEL_AUTHENTICATION
} GreeterMessage;

/* Messages from the server to the greeter */
typedef enum
{
    SERVER_MESSAGE_CONNECTED = 0,
    SERVER_MESSAGE_QUIT,
    SERVER_MESSAGE_PROMPT_AUTHENTICATION,
    SERVER_MESSAGE_END_AUTHENTICATION,
    SERVER_MESSAGE_SESSION_FAILED,
} ServerMessage;

/**
 * lightdm_greeter_new:
 *
 * Create a new greeter.
 *
 * Return value: the new #LightDMGreeter
 **/
LightDMGreeter *
lightdm_greeter_new ()
{
    return g_object_new (LIGHTDM_TYPE_GREETER, NULL);
}

static gboolean
timed_login_cb (gpointer data)
{
    LightDMGreeter *greeter = data;
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);

    priv->login_timeout = 0;
    g_signal_emit (G_OBJECT (greeter), signals[AUTOLOGIN_TIMER_EXPIRED], 0);

    return FALSE;
}

static guint32
int_length ()
{
    return 4;
}

static void
write_message (LightDMGreeter *greeter, guint8 *message, gsize message_length)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    GError *error = NULL;

    if (g_io_channel_write_chars (priv->to_server_channel, (gchar *) message, message_length, NULL, NULL) != G_IO_STATUS_NORMAL)
        g_warning ("Error writing to daemon: %s", error->message);
    else
        g_debug ("Wrote %zi bytes to daemon", message_length);
    g_clear_error (&error);
    g_io_channel_flush (priv->to_server_channel, NULL);
}

static void
write_int (guint8 *buffer, gint buffer_length, guint32 value, gsize *offset)
{
    if (*offset + 4 >= buffer_length)
        return;
    buffer[*offset] = value >> 24;
    buffer[*offset+1] = (value >> 16) & 0xFF;
    buffer[*offset+2] = (value >> 8) & 0xFF;
    buffer[*offset+3] = value & 0xFF;
    *offset += 4;
}

static void
write_string (guint8 *buffer, gint buffer_length, const gchar *value, gsize *offset)
{
    gint length = strlen (value);
    write_int (buffer, buffer_length, length, offset);
    if (*offset + length >= buffer_length)
        return;
    memcpy (buffer + *offset, value, length);
    *offset += length;
}

static guint32
read_int (LightDMGreeter *greeter, gsize *offset)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    guint32 value;
    guint8 *buffer;

    if (priv->n_read - *offset < int_length ())
    {
        g_warning ("Not enough space for int, need %i, got %zi", int_length (), priv->n_read - *offset);
        return 0;
    }

    buffer = priv->read_buffer + *offset;
    value = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
    *offset += int_length ();

    return value;
}

static gchar *
read_string (LightDMGreeter *greeter, gsize *offset)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    guint32 length;
    gchar *value;

    length = read_int (greeter, offset);
    if (priv->n_read - *offset < length)
    {
        g_warning ("Not enough space for string, need %u, got %zu", length, priv->n_read - *offset);
        return g_strdup ("");
    }

    value = g_malloc (sizeof (gchar) * (length + 1));
    memcpy (value, priv->read_buffer + *offset, length);
    value[length] = '\0';
    *offset += length;

    return value;
}

static guint32
string_length (const gchar *value)
{
    return int_length () + strlen (value);
}

static void
write_header (guint8 *buffer, gint buffer_length, guint32 id, guint32 length, gsize *offset)
{
    write_int (buffer, buffer_length, id, offset);
    write_int (buffer, buffer_length, length, offset);
}

static guint32 get_packet_length (LightDMGreeter *greeter)
{
    gsize offset = 4;
    return read_int (greeter, &offset);
}

static void
handle_connected (LightDMGreeter *greeter, guint32 length, gsize *offset)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    gchar *version;
    GString *hint_string;
    int timeout;

    version = read_string (greeter, offset);
    hint_string = g_string_new ("");
    while (*offset < length)
    {
        gchar *name, *value;
      
        name = read_string (greeter, offset);
        value = read_string (greeter, offset);
        g_hash_table_insert (priv->hints, name, value);
        g_string_append_printf (hint_string, " %s=%s", name, value);
    }

    g_debug ("Connected version=%s%s", version, hint_string->str);
    g_free (version);
    g_string_free (hint_string, TRUE);

    /* Set timeout for default login */
    timeout = lightdm_greeter_get_autologin_timeout_hint (greeter);
    if (timeout)
    {
        g_debug ("Setting autologin timer for %d seconds", timeout);
        priv->login_timeout = g_timeout_add (timeout * 1000, timed_login_cb, greeter);
    }
    g_signal_emit (G_OBJECT (greeter), signals[CONNECTED], 0);
}

static void
handle_prompt_authentication (LightDMGreeter *greeter, gsize *offset)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    guint32 sequence_number, n_messages, i;

    sequence_number = read_int (greeter, offset);
    if (sequence_number != priv->authenticate_sequence_number)
    {
        g_debug ("Ignoring prompt authentication with invalid sequence number %d", sequence_number);
        return;
    }

    if (priv->cancelling_authentication)
    {
        g_debug ("Ignoring prompt authentication as waiting for it to cancel");
        return;
    }

    n_messages = read_int (greeter, offset);
    g_debug ("Prompt user with %d message(s)", n_messages);

    for (i = 0; i < n_messages; i++)
    {
        int msg_style;
        gchar *msg;

        msg_style = read_int (greeter, offset);
        msg = read_string (greeter, offset);

        // FIXME: Should stop on prompts?
        switch (msg_style)
        {
        case PAM_PROMPT_ECHO_OFF:
            g_signal_emit (G_OBJECT (greeter), signals[SHOW_PROMPT], 0, msg, LIGHTDM_PROMPT_TYPE_SECRET);
            break;
        case PAM_PROMPT_ECHO_ON:
            g_signal_emit (G_OBJECT (greeter), signals[SHOW_PROMPT], 0, msg, LIGHTDM_PROMPT_TYPE_QUESTION);
            break;
        case PAM_ERROR_MSG:
            g_signal_emit (G_OBJECT (greeter), signals[SHOW_MESSAGE], 0, msg, LIGHTDM_MESSAGE_TYPE_ERROR);
            break;
        case PAM_TEXT_INFO:
            g_signal_emit (G_OBJECT (greeter), signals[SHOW_MESSAGE], 0, msg, LIGHTDM_MESSAGE_TYPE_INFO);
            break;
        }

        g_free (msg);
    }
}

static void
handle_end_authentication (LightDMGreeter *greeter, gsize *offset)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    guint32 sequence_number, return_code;

    sequence_number = read_int (greeter, offset);
    return_code = read_int (greeter, offset);

    if (sequence_number != priv->authenticate_sequence_number)
    {
        g_debug ("Ignoring end authentication with invalid sequence number %d", sequence_number);
        return;
    }

    g_debug ("Authentication complete with return code %d", return_code);
    priv->cancelling_authentication = FALSE;
    priv->is_authenticated = (return_code == 0);
    if (!priv->is_authenticated)
    {
        g_free (priv->authentication_user);
        priv->authentication_user = NULL;
    }
    g_signal_emit (G_OBJECT (greeter), signals[AUTHENTICATION_COMPLETE], 0);
    priv->in_authentication = FALSE;
}

static void
handle_session_failed (LightDMGreeter *greeter, gsize *offset)
{ 
    g_debug ("Session failed to start");
    g_signal_emit (G_OBJECT (greeter), signals[SESSION_FAILED], 0);
}

static void
handle_quit (LightDMGreeter *greeter, gsize *offset)
{
    g_debug ("Got quit request from server");
    g_signal_emit (G_OBJECT (greeter), signals[QUIT], 0);
}

static gboolean
read_packet (LightDMGreeter *greeter, gboolean block)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    gsize n_to_read, n_read;
    GError *error = NULL;

    /* Read the header, or the whole packet if we already have that */
    n_to_read = HEADER_SIZE;
    if (priv->n_read >= HEADER_SIZE)
        n_to_read += get_packet_length (greeter);

    do
    {
        GIOStatus status;
        status = g_io_channel_read_chars (priv->from_server_channel,
                                          (gchar *) priv->read_buffer + priv->n_read,
                                          n_to_read - priv->n_read,
                                          &n_read,
                                          &error);
        if (status == G_IO_STATUS_ERROR)
            g_warning ("Error reading from server: %s", error->message);
        g_clear_error (&error);
        if (status != G_IO_STATUS_NORMAL)
            break;

        g_debug ("Read %zi bytes from daemon", n_read);

        priv->n_read += n_read;
    } while (priv->n_read < n_to_read && block);

    /* Stop if haven't got all the data we want */
    if (priv->n_read != n_to_read)
        return FALSE;

    /* If have header, rerun for content */
    if (priv->n_read == HEADER_SIZE)
    {
        n_to_read = get_packet_length (greeter);
        if (n_to_read > 0)
        {
            priv->read_buffer = g_realloc (priv->read_buffer, HEADER_SIZE + n_to_read);
            return read_packet (greeter, block);
        }
    }

    return TRUE;
}

static gboolean
from_server_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    LightDMGreeter *greeter = data;
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    gsize offset;
    guint32 id, length;

    if (!read_packet (greeter, FALSE))
        return TRUE;

    offset = 0;
    id = read_int (greeter, &offset);
    length = read_int (greeter, &offset);
    switch (id)
    {
    case SERVER_MESSAGE_CONNECTED:
        handle_connected (greeter, length, &offset);
        break;
    case SERVER_MESSAGE_PROMPT_AUTHENTICATION:
        handle_prompt_authentication (greeter, &offset);
        break;
    case SERVER_MESSAGE_END_AUTHENTICATION:
        handle_end_authentication (greeter, &offset);
        break;
    case SERVER_MESSAGE_SESSION_FAILED:
        handle_session_failed (greeter, &offset);
        break;
    case SERVER_MESSAGE_QUIT:
        handle_quit (greeter, &offset);
        break;
    default:
        g_warning ("Unknown message from server: %d", id);
        break;
    }

    priv->n_read = 0;

    return TRUE;
}

/**
 * lightdm_greeter_connect_to_server:
 * @greeter: The greeter to connect
 *
 * Connects the greeter to the display manager.
 *
 * Return value: #TRUE if successfully connected
 **/
gboolean
lightdm_greeter_connect_to_server (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv;
    GError *error = NULL;
    const gchar *bus_address, *fd;
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    GBusType bus_type = G_BUS_TYPE_SYSTEM;

    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);

    priv = GET_PRIVATE (greeter);

    priv->system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!priv->system_bus)
        g_warning ("Failed to connect to system bus: %s", error->message);
    g_clear_error (&error);

    bus_address = getenv ("LIGHTDM_BUS");
    if (bus_address && strcmp (bus_address, "SESSION") == 0)
        bus_type = G_BUS_TYPE_SESSION;

    priv->lightdm_bus = g_bus_get_sync (bus_type, NULL, &error);
    if (!priv->lightdm_bus)
        g_warning ("Failed to connect to LightDM bus: %s", error->message);
    g_clear_error (&error);
    if (!priv->lightdm_bus)
        return FALSE;

    fd = getenv ("LIGHTDM_TO_SERVER_FD");
    if (!fd)
    {
        g_warning ("No LIGHTDM_TO_SERVER_FD environment variable");
        return FALSE;
    }
    priv->to_server_channel = g_io_channel_unix_new (atoi (fd));
    g_io_channel_set_encoding (priv->to_server_channel, NULL, NULL);

    fd = getenv ("LIGHTDM_FROM_SERVER_FD");
    if (!fd)
    {
        g_warning ("No LIGHTDM_FROM_SERVER_FD environment variable");
        return FALSE;
    }
    priv->from_server_channel = g_io_channel_unix_new (atoi (fd));
    g_io_channel_set_encoding (priv->from_server_channel, NULL, NULL);
    g_io_add_watch (priv->from_server_channel, G_IO_IN, from_server_cb, greeter);

    g_debug ("Connecting to display manager...");
    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_CONNECT, string_length (VERSION), &offset);
    write_string (message, MAX_MESSAGE_LENGTH, VERSION, &offset);
    write_message (greeter, message, offset);

    return TRUE;
}

/**
 * lightdm_greeter_get_hostname:
 * @greeter: a #LightDMGreeter
 *
 * Return value: The host this greeter is displaying
 **/
const gchar *
lightdm_greeter_get_hostname (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv;

    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);

    priv = GET_PRIVATE (greeter);

    if (!priv->hostname)
    {
        struct utsname info;
        uname (&info);
        priv->hostname = g_strdup (info.nodename);
    }

    return priv->hostname;
}

static LightDMUser *
get_user_by_name (LightDMGreeter *greeter, const gchar *username)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    GList *link;
  
    for (link = priv->users; link; link = link->next)
    {
        LightDMUser *user = link->data;
        if (strcmp (lightdm_user_get_name (user), username) == 0)
            return user;
    }

    return NULL;
}
  
static gint
compare_user (gconstpointer a, gconstpointer b)
{
    LightDMUser *user_a = (LightDMUser *) a, *user_b = (LightDMUser *) b;
    return strcmp (lightdm_user_get_display_name (user_a), lightdm_user_get_display_name (user_b));
}

static void
load_users (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    GKeyFile *config;
    gchar *value;
    gint minimum_uid;
    gchar **hidden_users, **hidden_shells;
    GList *users = NULL, *old_users, *new_users = NULL, *changed_users = NULL, *link;
    GError *error = NULL;

    g_debug ("Loading user config from %s", USER_CONFIG_FILE);

    config = g_key_file_new ();
    if (!g_key_file_load_from_file (config, USER_CONFIG_FILE, G_KEY_FILE_NONE, &error) &&
        !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Failed to load configuration from %s: %s", USER_CONFIG_FILE, error->message); // FIXME: Don't make warning on no file, just info
    g_clear_error (&error);

    if (g_key_file_has_key (config, "UserAccounts", "minimum-uid", NULL))
        minimum_uid = g_key_file_get_integer (config, "UserAccounts", "minimum-uid", NULL);
    else
        minimum_uid = 500;

    value = g_key_file_get_string (config, "UserAccounts", "hidden-users", NULL);
    if (!value)
        value = g_strdup ("nobody nobody4 noaccess");
    hidden_users = g_strsplit (value, " ", -1);
    g_free (value);

    value = g_key_file_get_string (config, "UserAccounts", "hidden-shells", NULL);
    if (!value)
        value = g_strdup ("/bin/false /usr/sbin/nologin");
    hidden_shells = g_strsplit (value, " ", -1);
    g_free (value);

    g_key_file_free (config);

    setpwent ();

    while (TRUE)
    {
        struct passwd *entry;
        LightDMUser *user;
        char **tokens;
        gchar *real_name, *image_path, *image;
        int i;

        errno = 0;
        entry = getpwent ();
        if (!entry)
            break;

        /* Ignore system users */
        if (entry->pw_uid < minimum_uid)
            continue;

        /* Ignore users disabled by shell */
        if (entry->pw_shell)
        {
            for (i = 0; hidden_shells[i] && strcmp (entry->pw_shell, hidden_shells[i]) != 0; i++);
            if (hidden_shells[i])
                continue;
        }

        /* Ignore certain users */
        for (i = 0; hidden_users[i] && strcmp (entry->pw_name, hidden_users[i]) != 0; i++);
        if (hidden_users[i])
            continue;

        tokens = g_strsplit (entry->pw_gecos, ",", -1);
        if (tokens[0] != NULL && tokens[0][0] != '\0')
            real_name = g_strdup (tokens[0]);
        else
            real_name = NULL;
        g_strfreev (tokens);
      
        image_path = g_build_filename (entry->pw_dir, ".face", NULL);
        if (!g_file_test (image_path, G_FILE_TEST_EXISTS))
        {
            g_free (image_path);
            image_path = g_build_filename (entry->pw_dir, ".face.icon", NULL);
            if (!g_file_test (image_path, G_FILE_TEST_EXISTS))
            {
                g_free (image_path);
                image_path = NULL;
            }
        }
        if (image_path)
            image = g_filename_to_uri (image_path, NULL, NULL);
        else
            image = NULL;
        g_free (image_path);

        user = lightdm_user_new (greeter, entry->pw_name, real_name, entry->pw_dir, image, FALSE);
        g_free (real_name);
        g_free (image);

        /* Update existing users if have them */
        for (link = priv->users; link; link = link->next)
        {
            LightDMUser *info = link->data;
            if (strcmp (lightdm_user_get_name (info), lightdm_user_get_name (user)) == 0)
            {
                if (lightdm_user_update (info, lightdm_user_get_real_name (user), lightdm_user_get_home_directory (user), lightdm_user_get_image (user), lightdm_user_get_logged_in (user)))
                    changed_users = g_list_insert_sorted (changed_users, info, compare_user);
                g_object_unref (user);
                user = info;
                break;
            }
        }
        if (!link)
        {
            /* Only notify once we have loaded the user list */
            if (priv->have_users)
                new_users = g_list_insert_sorted (new_users, user, compare_user);
        }
        users = g_list_insert_sorted (users, user, compare_user);
    }
    g_strfreev (hidden_users);
    g_strfreev (hidden_shells);

    if (errno != 0)
        g_warning ("Failed to read password database: %s", strerror (errno));

    endpwent ();

    /* Use new user list */
    old_users = priv->users;
    priv->users = users;
  
    /* Notify of changes */
    for (link = new_users; link; link = link->next)
    {
        LightDMUser *info = link->data;
        g_debug ("User %s added", lightdm_user_get_name (info));
        g_signal_emit (greeter, signals[USER_ADDED], 0, info);
    }
    g_list_free (new_users);
    for (link = changed_users; link; link = link->next)
    {
        LightDMUser *info = link->data;
        g_debug ("User %s changed", lightdm_user_get_name (info));
        g_signal_emit (greeter, signals[USER_CHANGED], 0, info);
    }
    g_list_free (changed_users);
    for (link = old_users; link; link = link->next)
    {
        GList *new_link;

        /* See if this user is in the current list */
        for (new_link = priv->users; new_link; new_link = new_link->next)
        {
            if (new_link->data == link->data)
                break;
        }

        if (!new_link)
        {
            LightDMUser *info = link->data;
            g_debug ("User %s removed", lightdm_user_get_name (info));
            g_signal_emit (greeter, signals[USER_REMOVED], 0, info);
            g_object_unref (info);
        }
    }
    g_list_free (old_users);
}

static void
passwd_changed_cb (GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, LightDMGreeter *greeter)
{
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
        g_debug ("%s changed, reloading user list", g_file_get_path (file));
        load_users (greeter);
    }
}

static void
update_users (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    GFile *passwd_file;
    GError *error = NULL;

    if (priv->have_users)
        return;

    load_users (greeter);

    /* Watch for changes to user list */
    passwd_file = g_file_new_for_path (PASSWD_FILE);
    priv->passwd_monitor = g_file_monitor (passwd_file, G_FILE_MONITOR_NONE, NULL, &error);
    g_object_unref (passwd_file);
    if (!priv->passwd_monitor)
        g_warning ("Error monitoring %s: %s", PASSWD_FILE, error->message);
    else
        g_signal_connect (priv->passwd_monitor, "changed", G_CALLBACK (passwd_changed_cb), greeter);
    g_clear_error (&error);

    priv->have_users = TRUE;
}

/**
 * lightdm_greeter_get_num_users:
 * @greeter: a #LightDMGreeter
 *
 * Return value: The number of users able to log in
 **/
gint
lightdm_greeter_get_num_users (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), 0);
    update_users (greeter);
    return g_list_length (GET_PRIVATE (greeter)->users);
}

/**
 * lightdm_greeter_get_users:
 * @greeter: A #LightDMGreeter
 *
 * Get a list of users to present to the user.  This list may be a subset of the
 * available users and may be empty depending on the server configuration.
 *
 * Return value: (element-type LightDMUser) (transfer none): A list of #LightDMUser that should be presented to the user.
 **/
GList *
lightdm_greeter_get_users (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    update_users (greeter);
    return GET_PRIVATE (greeter)->users;
}

/**
 * lightdm_greeter_get_user_by_name:
 * @greeter: A #LightDMGreeter
 * @username: Name of user to get.
 *
 * Get infomation about a given user or #NULL if this user doesn't exist.
 *
 * Return value: (transfer none): A #LightDMUser entry for the given user.
 **/
LightDMUser *
lightdm_greeter_get_user_by_name (LightDMGreeter *greeter, const gchar *username)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    g_return_val_if_fail (username != NULL, NULL);

    update_users (greeter);

    return get_user_by_name (greeter, username);
}

static void
update_languages (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    gchar *stdout_text = NULL, *stderr_text = NULL;
    gint exit_status;
    gboolean result;
    GError *error = NULL;

    if (priv->have_languages)
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
            LightDMLanguage *language;
            gchar *code;

            code = g_strchug (tokens[i]);
            if (code[0] == '\0')
                continue;

            /* Ignore the non-interesting languages */
            if (strcmp (code, "C") == 0 || strcmp (code, "POSIX") == 0)
                continue;

            language = lightdm_language_new (code);
            priv->languages = g_list_append (priv->languages, language);
        }

        g_strfreev (tokens);
    }

    g_clear_error (&error);
    g_free (stdout_text);
    g_free (stderr_text);

    priv->have_languages = TRUE;
}

/**
 * lightdm_greeter_get_default_language:
 * @greeter: A #LightDMGreeter
 *
 * Get the default language.
 *
 * Return value: The default language.
 **/
const gchar *
lightdm_greeter_get_default_language (LightDMGreeter *greeter)
{
    gchar *lang;
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    lang = getenv ("LANG");
    if (lang)
        return lang;
    else
        return "C";
}

/**
 * lightdm_greeter_get_languages:
 * @greeter: A #LightDMGreeter
 *
 * Get a list of languages to present to the user.
 *
 * Return value: (element-type LightDMLanguage) (transfer none): A list of #LightDMLanguage that should be presented to the user.
 **/
GList *
lightdm_greeter_get_languages (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    update_languages (greeter);
    return GET_PRIVATE (greeter)->languages;
}

static void
layout_cb (XklConfigRegistry *config,
           const XklConfigItem *item,
           gpointer data)
{
    LightDMGreeter *greeter = data;
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    LightDMLayout *layout;

    layout = lightdm_layout_new (item->name, item->short_description, item->description);
    priv->layouts = g_list_append (priv->layouts, layout);
}

static void
setup_display (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    if (!priv->display)
        priv->display = XOpenDisplay (NULL);
}

static void
setup_xkl (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);

    setup_display (greeter);

    priv->xkl_engine = xkl_engine_get_instance (priv->display);
    priv->xkl_config = xkl_config_rec_new ();
    if (!xkl_config_rec_get_from_server (priv->xkl_config, priv->xkl_engine))
        g_warning ("Failed to get Xkl configuration from server");
    priv->layout = g_strdup (priv->xkl_config->layouts[0]);
}

/**
 * lightdm_greeter_get_layouts:
 * @greeter: A #LightDMGreeter
 *
 * Get a list of keyboard layouts to present to the user.
 *
 * Return value: (element-type LightDMLayout) (transfer none): A list of #LightDMLayout that should be presented to the user.
 **/
GList *
lightdm_greeter_get_layouts (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv;
    XklConfigRegistry *registry;

    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);

    priv = GET_PRIVATE (greeter);

    if (priv->have_layouts)
        return priv->layouts;

    setup_xkl (greeter);

    registry = xkl_config_registry_get_instance (priv->xkl_engine);
    xkl_config_registry_load (registry, FALSE);
    xkl_config_registry_foreach_layout (registry, layout_cb, greeter);
    g_object_unref (registry);
    priv->have_layouts = TRUE;

    return priv->layouts;
}

/**
 * lightdm_greeter_set_layout:
 * @greeter: A #LightDMGreeter
 * @layout: The layout to use
 *
 * Set the layout for this session.
 **/
void
lightdm_greeter_set_layout (LightDMGreeter *greeter, const gchar *layout)
{
    LightDMGreeterPrivate *priv;
    XklConfigRec *config;

    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));
    g_return_if_fail (layout != NULL);

    priv = GET_PRIVATE (greeter);

    g_debug ("Setting keyboard layout to %s", layout);

    setup_xkl (greeter);

    config = xkl_config_rec_new ();
    config->layouts = g_malloc (sizeof (gchar *) * 2);
    config->model = g_strdup (priv->xkl_config->model);
    config->layouts[0] = g_strdup (layout);
    config->layouts[1] = NULL;
    if (!xkl_config_rec_activate (config, priv->xkl_engine))
        g_warning ("Failed to activate XKL config");
    else
        priv->layout = g_strdup (layout);
    g_object_unref (config);
}

/**
 * lightdm_greeter_get_layout:
 * @greeter: A #LightDMGreeter
 *
 * Get the current keyboard layout.
 *
 * Return value: The currently active layout for this user.
 **/
const gchar *
lightdm_greeter_get_layout (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    setup_xkl (greeter);
    return GET_PRIVATE (greeter)->layout;
}

static void
update_sessions (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    GDir *directory;
    GError *error = NULL;

    if (priv->have_sessions)
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
                priv->sessions = g_list_append (priv->sessions, lightdm_session_new (key, name, comment));
            }
            else
                g_warning ("Invalid session %s: %s", path, error->message);
            g_free (domain);
            g_free (name);
            g_free (comment);
        }

        g_free (key);
        g_free (path);
        g_key_file_free (key_file);
    }

    g_dir_close (directory);

    priv->have_sessions = TRUE;
}

/**
 * lightdm_greeter_get_sessions:
 * @greeter: A #LightDMGreeter
 *
 * Get the available sessions.
 *
 * Return value: (element-type LightDMSession) (transfer none): A list of #LightDMSession
 **/
GList *
lightdm_greeter_get_sessions (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    update_sessions (greeter);
    return GET_PRIVATE (greeter)->sessions;
}

/**
 * lightdm_greeter_get_hint:
 * @greeter: A #LightDMGreeter
 * @name: The hint name to query.
 *
 * Get a hint.
 *
 * Return value: The value for this hint or #NULL if not set.
 **/
const gchar *
lightdm_greeter_get_hint (LightDMGreeter *greeter, const gchar *name)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    return g_hash_table_lookup (GET_PRIVATE (greeter)->hints, name);
}

/**
 * lightdm_greeter_get_default_session_hint:
 * @greeter: A #LightDMGreeter
 *
 * Get the default session to use.
 *
 * Return value: The session name
 **/
const gchar *
lightdm_greeter_get_default_session_hint (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    return lightdm_greeter_get_hint (greeter, "default-session");
}

/**
 * lightdm_greeter_get_hide_users_hint:
 * @greeter: A #LightDMGreeter
 *
 * Check if user accounts should be shown.
 *
 * Return value: #TRUE if the available users should not be shown.
 */
gboolean
lightdm_greeter_get_hide_users_hint (LightDMGreeter *greeter)
{
    const gchar *value;

    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    value = lightdm_greeter_get_hint (greeter, "hide-users");

    return g_strcmp0 (value, "true") == 0;
}

/**
 * lightdm_greeter_get_has_guest_account_hint:
 * @greeter: A #LightDMGreeter
 *
 * Check if guest sessions are supported.
 *
 * Return value: #TRUE if guest sessions are supported.
 */
gboolean
lightdm_greeter_get_has_guest_account_hint (LightDMGreeter *greeter)
{
    const gchar *value;

    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    value = lightdm_greeter_get_hint (greeter, "has-guest-account");
  
    return g_strcmp0 (value, "true") == 0;
}

/**
 * lightdm_greeter_get_select_user_hint:
 * @greeter: A #LightDMGreeter
 *
 * Get the user to select by default.
 *
 * Return value: A username
 */
const gchar *
lightdm_greeter_get_select_user_hint (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    return lightdm_greeter_get_hint (greeter, "select-user");
}

/**
 * lightdm_greeter_get_select_guest_hint:
 * @greeter: A #LightDMGreeter
 *
 * Check if the guest account should be selected by default.
 *
 * Return value: #TRUE if the guest account should be selected by default.
 */
gboolean
lightdm_greeter_get_select_guest_hint (LightDMGreeter *greeter)
{
    const gchar *value;

    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    value = lightdm_greeter_get_hint (greeter, "select-guest");
  
    return g_strcmp0 (value, "true") == 0;
}

/**
 * lightdm_greeter_get_autologin_user_hint:
 * @greeter: A #LightDMGreeter
 *
 * Get the user account to automatically logg into when the timer expires.
 *
 * Return value: The user account to automatically log into.
 */
const gchar *
lightdm_greeter_get_autologin_user_hint (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    return lightdm_greeter_get_hint (greeter, "autologin-user");
}

/**
 * lightdm_greeter_get_autologin_guest_hint:
 * @greeter: A #LightDMGreeter
 *
 * Check if the guest account should be automatically logged into when the timer expires.
 *
 * Return value: #TRUE if the guest account should be automatically logged into.
 */
gboolean
lightdm_greeter_get_autologin_guest_hint (LightDMGreeter *greeter)
{
    const gchar *value;

    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    value = lightdm_greeter_get_hint (greeter, "autologin-guest");
  
    return g_strcmp0 (value, "true") == 0;
}

/**
 * lightdm_greeter_get_autologin_timeout_hint:
 * @greeter: A #LightDMGreeter
 *
 * Get the number of seconds to wait before automaitcally logging in.
 *
 * Return value: The number of seconds to wait before automatically logging in or 0 for no timeout.
 */
gint
lightdm_greeter_get_autologin_timeout_hint (LightDMGreeter *greeter)
{
    const gchar *value;
    gint timeout = 0;

    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    value = lightdm_greeter_get_hint (greeter, "autologin-timeout");
    if (value)
        timeout = atoi (value);
    if (timeout < 0)
        timeout = 0;

    return timeout;
}

/**
 * lightdm_greeter_cancel_timed_login:
 * @greeter: A #LightDMGreeter
 *
 * Cancel the login as the default user.
 */
void
lightdm_greeter_cancel_timed_login (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv;

    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));

    priv = GET_PRIVATE (greeter);

    if (priv->login_timeout)
       g_source_remove (priv->login_timeout);
    priv->login_timeout = 0;
}

/**
 * lightdm_greeter_login:
 * @greeter: A #LightDMGreeter
 * @username: (allow-none): A username or #NULL to prompt for a username.
 *
 * Starts the authentication procedure for a user.
 **/
void
lightdm_greeter_login (LightDMGreeter *greeter, const char *username)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));

    if (!username)
        username = "";

    priv->cancelling_authentication = FALSE;
    priv->authenticate_sequence_number++;
    priv->in_authentication = TRUE;  
    priv->is_authenticated = FALSE;
    g_free (priv->authentication_user);
    priv->authentication_user = g_strdup (username);

    g_debug ("Starting authentication for user %s...", username);
    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_LOGIN, int_length () + string_length (username), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, priv->authenticate_sequence_number, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, username, &offset);
    write_message (greeter, message, offset);
}

/**
 * lightdm_greeter_login_with_user_prompt:
 * @greeter: A #LightDMGreeter
 *
 * Starts the authentication procedure, prompting the greeter for a username.
 **/
void
lightdm_greeter_login_with_user_prompt (LightDMGreeter *greeter)
{
    lightdm_greeter_login (greeter, NULL);
}

/**
 * lightdm_greeter_login_as_guest:
 * @greeter: A #LightDMGreeter
 *
 * Starts the authentication procedure for the guest user.
 **/
void
lightdm_greeter_login_as_guest (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv;
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));

    priv = GET_PRIVATE (greeter);

    priv->cancelling_authentication = FALSE;
    priv->authenticate_sequence_number++;
    priv->in_authentication = TRUE;
    priv->is_authenticated = FALSE;
    g_free (priv->authentication_user);
    priv->authentication_user = NULL;

    g_debug ("Starting authentication for guest account...");
    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_LOGIN_AS_GUEST, int_length (), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, priv->authenticate_sequence_number, &offset);
    write_message (greeter, message, offset);
}

/**
 * lightdm_greeter_respond:
 * @greeter: A #LightDMGreeter
 * @response: Response to a prompt
 *
 * Provide response to a prompt.
 **/
void
lightdm_greeter_respond (LightDMGreeter *greeter, const gchar *response)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));
    g_return_if_fail (response != NULL);

    g_debug ("Providing response to display manager");
    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_CONTINUE_AUTHENTICATION, int_length () + string_length (response), &offset);
    // FIXME: Could be multiple responses required
    write_int (message, MAX_MESSAGE_LENGTH, 1, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, response, &offset);
    write_message (greeter, message, offset);
}

/**
 * lightdm_greeter_cancel_authentication:
 * @greeter: A #LightDMGreeter
 *
 * Cancel the current user authentication.
 **/
void
lightdm_greeter_cancel_authentication (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv;
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));

    priv = GET_PRIVATE (greeter);

    priv->cancelling_authentication = TRUE;
    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_CANCEL_AUTHENTICATION, 0, &offset);
    write_message (greeter, message, offset);
}

/**
 * lightdm_greeter_get_in_authentication:
 * @greeter: A #LightDMGreeter
 *
 * Checks if the greeter is in the process of authenticating.
 *
 * Return value: #TRUE if the greeter is authenticating a user.
 **/
gboolean
lightdm_greeter_get_in_authentication (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    return GET_PRIVATE (greeter)->in_authentication;
}

/**
 * lightdm_greeter_get_is_authenticated:
 * @greeter: A #LightDMGreeter
 *
 * Checks if the greeter has successfully authenticated.
 *
 * Return value: #TRUE if the greeter is authenticated for login.
 **/
gboolean
lightdm_greeter_get_is_authenticated (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    return GET_PRIVATE (greeter)->is_authenticated;
}

/**
 * lightdm_greeter_get_authentication_user:
 * @greeter: A #LightDMGreeter
 *
 * Get the user that is being authenticated.
 *
 * Return value: The username of the authentication user being authenticated or #NULL if no authentication in progress.
 */
const gchar *
lightdm_greeter_get_authentication_user (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), NULL);
    return GET_PRIVATE (greeter)->authentication_user;
}

/**
 * lightdm_greeter_start_session:
 * @greeter: A #LightDMGreeter
 * @session: (allow-none): The session to log into or #NULL to use the default
 *
 * Start a session for the logged in user.
 **/
void
lightdm_greeter_start_session (LightDMGreeter *greeter, const gchar *session)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));
  
    if (!session)
        session = "";

    g_debug ("Starting session %s", session);
    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_START_SESSION, string_length (session), &offset);
    write_string (message, MAX_MESSAGE_LENGTH, session, &offset);
    write_message (greeter, message, offset);
}

/**
 * lightdm_greeter_start_session_with_defaults:
 * @greeter: A #LightDMGreeter
 *
 * Login a user to a session using default settings for that user.
 **/
void
lightdm_greeter_start_default_session (LightDMGreeter *greeter)
{
    lightdm_greeter_start_session (greeter, NULL);
}

static gboolean
upower_call_function (LightDMGreeter *greeter, const gchar *function, gboolean has_result)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    GDBusProxy *proxy;
    GVariant *result;
    GError *error = NULL;
    gboolean function_result = FALSE;
  
    if (!priv->system_bus)
        return FALSE;

    proxy = g_dbus_proxy_new_sync (priv->system_bus,
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

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(b)")))
        g_variant_get (result, "(b)", &function_result);

    g_variant_unref (result);
    return function_result;
}

/**
 * lightdm_greeter_get_can_suspend:
 * @greeter: A #LightDMGreeter
 *
 * Checks if the greeter is authorized to do a system suspend.
 *
 * Return value: #TRUE if the greeter can suspend the system
 **/
gboolean
lightdm_greeter_get_can_suspend (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    return upower_call_function (greeter, "SuspendAllowed", TRUE);
}

/**
 * lightdm_greeter_suspend:
 * @greeter: A #LightDMGreeter
 *
 * Triggers a system suspend.
 **/
void
lightdm_greeter_suspend (LightDMGreeter *greeter)
{
    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));
    upower_call_function (greeter, "Suspend", FALSE);
}

/**
 * lightdm_greeter_get_can_hibernate:
 * @greeter: A #LightDMGreeter
 *
 * Checks if the greeter is authorized to do a system hibernate.
 *
 * Return value: #TRUE if the greeter can hibernate the system
 **/
gboolean
lightdm_greeter_get_can_hibernate (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    return upower_call_function (greeter, "HibernateAllowed", TRUE);
}

/**
 * lightdm_greeter_hibernate:
 * @greeter: A #LightDMGreeter
 *
 * Triggers a system hibernate.
 **/
void
lightdm_greeter_hibernate (LightDMGreeter *greeter)
{
    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));
    upower_call_function (greeter, "Hibernate", FALSE);
}

static gboolean
ck_call_function (LightDMGreeter *greeter, const gchar *function, gboolean has_result)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);
    GDBusProxy *proxy;
    GVariant *result;
    GError *error = NULL;
    gboolean function_result = FALSE;

    if (!priv->system_bus)
        return FALSE;

    proxy = g_dbus_proxy_new_sync (priv->system_bus,
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

    if (g_variant_is_of_type (result, G_VARIANT_TYPE ("(b)")))
        g_variant_get (result, "(b)", &function_result);

    g_variant_unref (result);
    return function_result;
}

/**
 * lightdm_greeter_get_can_restart:
 * @greeter: A #LightDMGreeter
 *
 * Checks if the greeter is authorized to do a system restart.
 *
 * Return value: #TRUE if the greeter can restart the system
 **/
gboolean
lightdm_greeter_get_can_restart (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    return ck_call_function (greeter, "CanRestart", TRUE);
}

/**
 * lightdm_greeter_restart:
 * @greeter: A #LightDMGreeter
 *
 * Triggers a system restart.
 **/
void
lightdm_greeter_restart (LightDMGreeter *greeter)
{
    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));
    ck_call_function (greeter, "Restart", FALSE);
}

/**
 * lightdm_greeter_get_can_shutdown:
 * @greeter: A #LightDMGreeter
 *
 * Checks if the greeter is authorized to do a system shutdown.
 *
 * Return value: #TRUE if the greeter can shutdown the system
 **/
gboolean
lightdm_greeter_get_can_shutdown (LightDMGreeter *greeter)
{
    g_return_val_if_fail (LIGHTDM_IS_GREETER (greeter), FALSE);
    return ck_call_function (greeter, "CanStop", TRUE);
}

/**
 * lightdm_greeter_shutdown:
 * @greeter: A #LightDMGreeter
 *
 * Triggers a system shutdown.
 **/
void
lightdm_greeter_shutdown (LightDMGreeter *greeter)
{
    g_return_if_fail (LIGHTDM_IS_GREETER (greeter));
    ck_call_function (greeter, "Stop", FALSE);
}

static void
lightdm_greeter_init (LightDMGreeter *greeter)
{
    LightDMGreeterPrivate *priv = GET_PRIVATE (greeter);

    priv->read_buffer = g_malloc (HEADER_SIZE);
    priv->hints = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    g_debug ("default-language=%s", lightdm_greeter_get_default_language (greeter));
}

static void
lightdm_greeter_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LightDMGreeter *self;

    self = LIGHTDM_GREETER (object);

    switch (prop_id) {
    case PROP_LAYOUT:
        lightdm_greeter_set_layout(self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lightdm_greeter_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LightDMGreeter *self;

    self = LIGHTDM_GREETER (object);

    switch (prop_id) {
    case PROP_HOSTNAME:
        g_value_set_string (value, lightdm_greeter_get_hostname (self));
        break;
    case PROP_NUM_USERS:
        g_value_set_int (value, lightdm_greeter_get_num_users (self));
        break;
    case PROP_USERS:
        break;
    case PROP_DEFAULT_LANGUAGE:
        g_value_set_string (value, lightdm_greeter_get_default_language (self));
        break;
    case PROP_LAYOUTS:
        break;
    case PROP_LAYOUT:
        g_value_set_string (value, lightdm_greeter_get_layout (self));
        break;
    case PROP_SESSIONS:
        break;
    case PROP_DEFAULT_SESSION_HINT:
        g_value_set_string (value, lightdm_greeter_get_default_session_hint (self));
        break;
    case PROP_HIDE_USERS_HINT:
        g_value_set_boolean (value, lightdm_greeter_get_hide_users_hint (self));
        break;
    case PROP_HAS_GUEST_ACCOUNT_HINT:
        g_value_set_boolean (value, lightdm_greeter_get_has_guest_account_hint (self));
        break;
    case PROP_SELECT_USER_HINT:
        g_value_set_string (value, lightdm_greeter_get_select_user_hint (self));
        break;
    case PROP_SELECT_GUEST_HINT:
        g_value_set_boolean (value, lightdm_greeter_get_select_guest_hint (self));
        break;
    case PROP_AUTOLOGIN_USER_HINT:
        g_value_set_string (value, lightdm_greeter_get_autologin_user_hint (self));
        break;
    case PROP_AUTOLOGIN_GUEST_HINT:
        g_value_set_boolean (value, lightdm_greeter_get_autologin_guest_hint (self));
        break;
    case PROP_AUTOLOGIN_TIMEOUT_HINT:
        g_value_set_int (value, lightdm_greeter_get_autologin_timeout_hint (self));
        break;
    case PROP_AUTHENTICATION_USER:
        g_value_set_string (value, lightdm_greeter_get_authentication_user (self));
        break;
    case PROP_IN_AUTHENTICATION:
        g_value_set_boolean (value, lightdm_greeter_get_in_authentication (self));
        break;
    case PROP_IS_AUTHENTICATED:
        g_value_set_boolean (value, lightdm_greeter_get_is_authenticated (self));
        break;
    case PROP_CAN_SUSPEND:
        g_value_set_boolean (value, lightdm_greeter_get_can_suspend (self));
        break;
    case PROP_CAN_HIBERNATE:
        g_value_set_boolean (value, lightdm_greeter_get_can_hibernate (self));
        break;
    case PROP_CAN_RESTART:
        g_value_set_boolean (value, lightdm_greeter_get_can_restart (self));
        break;
    case PROP_CAN_SHUTDOWN:
        g_value_set_boolean (value, lightdm_greeter_get_can_shutdown (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
marshal_VOID__STRING_INT (GClosure     *closure,
                          GValue       *return_value G_GNUC_UNUSED,
                          guint         n_param_values,
                          const GValue *param_values,
                          gpointer      invocation_hint G_GNUC_UNUSED,
                          gpointer      marshal_data)
{
    typedef void (*GMarshalFunc_VOID__STRING_INT) (gpointer     data1,
                                                   gpointer     arg_1,
                                                   gint         arg_2,
                                                   gpointer     data2);
    register GMarshalFunc_VOID__STRING_INT callback;
    register GCClosure *cc = (GCClosure*) closure;
    register gpointer data1, data2;

    g_return_if_fail (n_param_values == 3);

    if (G_CCLOSURE_SWAP_DATA (closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer (param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer (param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_VOID__STRING_INT) (marshal_data ? marshal_data : cc->callback);

    callback (data1,
              (param_values + 1)->data[0].v_pointer,
              (param_values + 2)->data[0].v_int,
              data2);
}

static void
lightdm_greeter_finalize (GObject *object)
{
    LightDMGreeter *self = LIGHTDM_GREETER (object);
    LightDMGreeterPrivate *priv = GET_PRIVATE (self);

    g_hash_table_unref (priv->hints);

    G_OBJECT_CLASS (lightdm_greeter_parent_class)->finalize (object);
}

static void
lightdm_greeter_class_init (LightDMGreeterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (LightDMGreeterPrivate));

    object_class->set_property = lightdm_greeter_set_property;
    object_class->get_property = lightdm_greeter_get_property;
    object_class->finalize = lightdm_greeter_finalize;

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
                                     PROP_DEFAULT_SESSION_HINT,
                                     g_param_spec_string ("default-session-hint",
                                                          "default-session-hint",
                                                          "Default session hint",
                                                          NULL,
                                                          G_PARAM_READWRITE));

    g_object_class_install_property (object_class,
                                     PROP_HIDE_USERS_HINT,
                                     g_param_spec_boolean ("hide-users-hint",
                                                           "hide-users-hint",
                                                           "hide users hint",
                                                           FALSE,
                                                           G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     PROP_HAS_GUEST_ACCOUNT_HINT,
                                     g_param_spec_boolean ("has-guest-account-hint",
                                                           "has-guest-account-hint",
                                                           "Has guest account hint",
                                                           FALSE,
                                                           G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     PROP_SELECT_USER_HINT,
                                     g_param_spec_string ("select-user-hint",
                                                          "select-user-hint",
                                                          "Select user hint",
                                                          NULL,
                                                          G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     PROP_SELECT_GUEST_HINT,
                                     g_param_spec_boolean ("select-guest-hint",
                                                           "select-guest-hint",
                                                           "Select guest account hint",
                                                           FALSE,
                                                           G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     PROP_AUTOLOGIN_USER_HINT,
                                     g_param_spec_string ("autologin-user-hint",
                                                          "autologin-user-hint",
                                                          "Autologin user hint",
                                                          NULL,
                                                          G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     PROP_AUTOLOGIN_GUEST_HINT,
                                     g_param_spec_boolean ("autologin-guest-hint",
                                                           "autologin-guest-hint",
                                                           "Autologin guest account hint",
                                                           FALSE,
                                                           G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     PROP_AUTOLOGIN_TIMEOUT_HINT,
                                     g_param_spec_int ("autologin-timeout-hint",
                                                       "autologin-timeout-hint",
                                                       "Autologin timeout hint",
                                                       0, G_MAXINT, 0,
                                                       G_PARAM_READABLE));

    g_object_class_install_property (object_class,
                                     PROP_AUTHENTICATION_USER,
                                     g_param_spec_string ("authentication-user",
                                                          "authentication-user",
                                                          "The user being authenticated",
                                                          NULL,
                                                          G_PARAM_READABLE));
    g_object_class_install_property (object_class,
                                     PROP_IN_AUTHENTICATION,
                                     g_param_spec_boolean ("in-authentication",
                                                           "in-authentication",
                                                           "TRUE if a user is being authenticated",
                                                           FALSE,
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
     * LightDMGreeter::connected:
     * @greeter: A #LightDMGreeter
     *
     * The ::connected signal gets emitted when the greeter connects to the
     * LightDM server.
     **/
    signals[CONNECTED] =
        g_signal_new ("connected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, connected),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    /**
     * LightDMGreeter::show-prompt:
     * @greeter: A #LightDMGreeter
     * @text: Prompt text
     * @type: Prompt type
     *
     * The ::show-prompt signal gets emitted when the greeter should show a
     * prompt to the user.  The given text should be displayed and an input
     * field for the user to provide a response.
     *
     * Call lightdm_greeter_respond() with the resultant input or
     * lightdm_greeter_cancel_authentication() to abort the authentication.
     **/
    signals[SHOW_PROMPT] =
        g_signal_new ("show-prompt",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, show_prompt),
                      NULL, NULL,
                      marshal_VOID__STRING_INT,
                      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);

    /**
     * LightDMGreeter::show-message:
     * @greeter: A #LightDMGreeter
     * @text: Message text
     * @type: Message type
     *
     * The ::show-message signal gets emitted when the greeter
     * should show a message to the user.
     **/
    signals[SHOW_MESSAGE] =
        g_signal_new ("show-message",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, show_message),
                      NULL, NULL,
                      marshal_VOID__STRING_INT,
                      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);

    /**
     * LightDMGreeter::authentication-complete:
     * @greeter: A #LightDMGreeter
     *
     * The ::authentication-complete signal gets emitted when the greeter
     * has completed authentication.
     *
     * Call lightdm_greeter_get_is_authenticated() to check if the authentication
     * was successful.
     **/
    signals[AUTHENTICATION_COMPLETE] =
        g_signal_new ("authentication-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, authentication_complete),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    /**
     * LightDMGreeter::session-failed:
     * @greeter: A #LightDMGreeter
     *
     * The ::session-failed signal gets emitted when the deamon has failed
     * to start the requested session.
     **/
    signals[SESSION_FAILED] =
        g_signal_new ("session-failed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, session_failed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    /**
     * LightDMGreeter::autologin-timer-expired:
     * @greeter: A #LightDMGreeter
     * @username: A username
     *
     * The ::timed-login signal gets emitted when the automatic login timer has expired.
     * The application should then call lightdm_greeter_login().
     **/
    signals[AUTOLOGIN_TIMER_EXPIRED] =
        g_signal_new ("autologin-timer-expired",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, autologin_timer_expired),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    /**
     * LightDMGreeter::user-added:
     * @greeter: A #LightDMGreeter
     *
     * The ::user-added signal gets emitted when a user account is created.
     **/
    signals[USER_ADDED] =
        g_signal_new ("user-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, user_added),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);

    /**
     * LightDMGreeter::user-changed:
     * @greeter: A #LightDMGreeter
     *
     * The ::user-changed signal gets emitted when a user account is modified.
     **/
    signals[USER_CHANGED] =
        g_signal_new ("user-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, user_changed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);

    /**
     * LightDMGreeter::user-removed:
     * @greeter: A #LightDMGreeter
     *
     * The ::user-removed signal gets emitted when a user account is removed.
     **/
    signals[USER_REMOVED] =
        g_signal_new ("user-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, user_removed),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1, LIGHTDM_TYPE_USER);

    /**
     * LightDMGreeter::quit:
     * @greeter: A #LightDMGreeter
     *
     * The ::quit signal gets emitted when the greeter should exit.
     **/
    signals[QUIT] =
        g_signal_new ("quit",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LightDMGreeterClass, quit),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
