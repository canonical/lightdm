/*
 * Copyright (C) 2010-2016 Canonical Ltd.
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
#include <gcrypt.h>

#include "greeter.h"
#include "configuration.h"
#include "shared-data-manager.h"

enum {
    PROP_ACTIVE_USERNAME = 1,
};

enum {
    CONNECTED,
    DISCONNECTED,
    CREATE_SESSION,
    START_SESSION,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    /* PAM service to authenticate with */
    gchar *pam_service;
    gchar *autologin_pam_service;

    /* Buffer for data read from greeter */
    guint8 *read_buffer;
    gsize n_read;
    gboolean use_secure_memory;

    /* Hints for the greeter */
    GHashTable *hints;

    /* Default session to use */
    gchar *default_session;

    /* Sequence number of current PAM session */
    guint32 authentication_sequence_number;

    /* Remote session name */
    gchar *remote_session;

    /* Currently selected user */
    gchar *active_username;

    /* PAM session being constructed by the greeter */
    Session *authentication_session;

    /* API version the client can speak */
    guint32 api_version;

    /* TRUE if a the greeter can handle a reset; else we will just kill it instead */
    gboolean resettable;

    /* TRUE if a user has been authenticated and the session requested to start */
    gboolean start_session;

    /* TRUE if can log into guest accounts */
    gboolean allow_guest;

    /* TRUE if logging into guest session */
    gboolean guest_account_authenticated;

    /* Communication channels to communicate with */
    int to_greeter_input;
    int from_greeter_output;
    GIOChannel *to_greeter_channel;
    GIOChannel *from_greeter_channel;
    guint from_greeter_watch;
} GreeterPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (Greeter, greeter, G_TYPE_OBJECT)

#define API_VERSION 1

/* Messages from the greeter to the server */
typedef enum
{
    GREETER_MESSAGE_CONNECT = 0,
    GREETER_MESSAGE_AUTHENTICATE,
    GREETER_MESSAGE_AUTHENTICATE_AS_GUEST,
    GREETER_MESSAGE_CONTINUE_AUTHENTICATION,
    GREETER_MESSAGE_START_SESSION,
    GREETER_MESSAGE_CANCEL_AUTHENTICATION,
    GREETER_MESSAGE_SET_LANGUAGE,
    GREETER_MESSAGE_AUTHENTICATE_REMOTE,
    GREETER_MESSAGE_ENSURE_SHARED_DIR,
} GreeterMessage;

/* Messages from the server to the greeter */
typedef enum
{
    SERVER_MESSAGE_CONNECTED = 0,
    SERVER_MESSAGE_PROMPT_AUTHENTICATION,
    SERVER_MESSAGE_END_AUTHENTICATION,
    SERVER_MESSAGE_SESSION_RESULT,
    SERVER_MESSAGE_SHARED_DIR_RESULT,
    SERVER_MESSAGE_IDLE,
    SERVER_MESSAGE_RESET,
    SERVER_MESSAGE_CONNECTED_V2,
} ServerMessage;

static gboolean read_cb (GIOChannel *source, GIOCondition condition, gpointer data);

Greeter *
greeter_new (void)
{
    return g_object_new (GREETER_TYPE, NULL);
}

void
greeter_set_file_descriptors (Greeter *greeter, int to_greeter_fd, int from_greeter_fd)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    g_return_if_fail (greeter != NULL);
    g_return_if_fail (priv->to_greeter_input < 0);
    g_return_if_fail (priv->from_greeter_output < 0);

    priv->to_greeter_input = to_greeter_fd;
    priv->to_greeter_channel = g_io_channel_unix_new (priv->to_greeter_input);
    g_autoptr(GError) to_error = NULL;
    g_io_channel_set_encoding (priv->to_greeter_channel, NULL, &to_error);
    if (to_error)
        g_warning ("Failed to set encoding on to greeter channel to binary: %s\n", to_error->message);

    priv->from_greeter_output = from_greeter_fd;
    priv->from_greeter_channel = g_io_channel_unix_new (priv->from_greeter_output);
    g_autoptr(GError) from_error = NULL;
    g_io_channel_set_encoding (priv->from_greeter_channel, NULL, &from_error);
    if (from_error)
        g_warning ("Failed to set encoding on from greeter channel to binary: %s\n", from_error->message);
    g_io_channel_set_buffered (priv->from_greeter_channel, FALSE);

    priv->from_greeter_watch = g_io_add_watch (priv->from_greeter_channel, G_IO_IN | G_IO_HUP, read_cb, greeter);
}

void
greeter_stop (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    /* Stop any events occurring after we've stopped */
    if (priv->authentication_session)
        g_signal_handlers_disconnect_matched (priv->authentication_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, greeter);
}

void
greeter_set_pam_services (Greeter *greeter, const gchar *pam_service, const gchar *autologin_pam_service)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    g_return_if_fail (greeter != NULL);
    g_free (priv->pam_service);
    priv->pam_service = g_strdup (pam_service);
    g_free (priv->autologin_pam_service);
    priv->autologin_pam_service = g_strdup (autologin_pam_service);
}

void
greeter_set_allow_guest (Greeter *greeter, gboolean allow_guest)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    g_return_if_fail (greeter != NULL);
    priv->allow_guest = allow_guest;
}

void
greeter_clear_hints (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    g_return_if_fail (greeter != NULL);
    g_hash_table_remove_all (priv->hints);
}

void
greeter_set_hint (Greeter *greeter, const gchar *name, const gchar *value)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    g_return_if_fail (greeter != NULL);
    g_hash_table_insert (priv->hints, g_strdup (name), g_strdup (value));
}

static void *
secure_malloc (Greeter *greeter, size_t n)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    if (priv->use_secure_memory)
        return gcry_malloc_secure (n);
    else
        return g_malloc (n);
}

static void *
secure_realloc (Greeter *greeter, void *ptr, size_t n)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    if (priv->use_secure_memory)
        return gcry_realloc (ptr, n);
    else
        return g_realloc (ptr, n);
}

static void
secure_free (Greeter *greeter, void *ptr)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    if (priv->use_secure_memory)
        return gcry_free (ptr);
    else
        return g_free (ptr);
}

static void
secure_freev (Greeter *greeter, gchar **v)
{
    for (int i = 0; v[i]; i++)
        secure_free (greeter, v[i]);
    g_free (v);
}

static guint32
int_length (void)
{
    return 4;
}

#define HEADER_SIZE (sizeof (guint32) * 2)
#define MAX_MESSAGE_LENGTH 1024

static void
write_message (Greeter *greeter, guint8 *message, gsize message_length)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    gchar *data = (gchar *) message;
    gsize data_length = message_length;
    while (data_length > 0)
    {
        GIOStatus status;
        gsize n_written;

        g_autoptr(GError) error = NULL;
        status = g_io_channel_write_chars (priv->to_greeter_channel, data, data_length, &n_written, &error);
        if (error)
            g_warning ("Error writing to greeter: %s", error->message);
        if (status != G_IO_STATUS_NORMAL)
            return;
        data_length -= n_written;
        data += n_written;
    }

    g_autoptr(GError) error = NULL;
    g_io_channel_flush (priv->to_greeter_channel, &error);
    if (error)
        g_warning ("Failed to flush data to greeter: %s", error->message);
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
    gint length;
    if (value)
        length = strlen (value);
    else
        length = 0;
    write_int (buffer, buffer_length, length, offset);
    if (*offset + length >= buffer_length)
        return;
    if (length > 0)
    {
        memcpy (buffer + *offset, value, length);
        *offset += length;
    }
}

static void
write_header (guint8 *buffer, gint buffer_length, guint32 id, guint32 length, gsize *offset)
{
    write_int (buffer, buffer_length, id, offset);
    write_int (buffer, buffer_length, length, offset);
}

static guint32
string_length (const gchar *value)
{
    if (value == NULL)
        return int_length ();
    else
        return int_length () + strlen (value);
}

static void
handle_connect (Greeter *greeter, const gchar *version, gboolean resettable, guint32 api_version)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    g_debug ("Greeter connected version=%s api=%u resettable=%s", version, api_version, resettable ? "true" : "false");

    priv->api_version = api_version;
    priv->resettable = resettable;

    guint32 env_length = 0;
    GHashTableIter iter;
    g_hash_table_iter_init (&iter, priv->hints);
    gpointer key, value;
    while (g_hash_table_iter_next (&iter, &key, &value))
        env_length += string_length (key) + string_length (value);

    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    if (api_version == 0)
    {
        write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_CONNECTED, string_length (VERSION) + env_length, &offset);
        write_string (message, MAX_MESSAGE_LENGTH, VERSION, &offset);
        g_hash_table_iter_init (&iter, priv->hints);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            write_string (message, MAX_MESSAGE_LENGTH, key, &offset);
            write_string (message, MAX_MESSAGE_LENGTH, value, &offset);
        }
    }
    else
    {
        write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_CONNECTED_V2, string_length (VERSION) + int_length () * 2 + env_length, &offset);
        write_int (message, MAX_MESSAGE_LENGTH, api_version <= API_VERSION ? api_version : API_VERSION, &offset);
        write_string (message, MAX_MESSAGE_LENGTH, VERSION, &offset);
        write_int (message, MAX_MESSAGE_LENGTH, g_hash_table_size (priv->hints), &offset);
        g_hash_table_iter_init (&iter, priv->hints);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            write_string (message, MAX_MESSAGE_LENGTH, key, &offset);
            write_string (message, MAX_MESSAGE_LENGTH, value, &offset);
        }
    }
    write_message (greeter, message, offset);

    g_signal_emit (greeter, signals[CONNECTED], 0);
}

static void
pam_messages_cb (Session *session, Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    const struct pam_message *messages = session_get_messages (session);
    int messages_length = session_get_messages_length (session);

    /* Respond to d-bus query with messages */
    g_debug ("Prompt greeter with %d message(s)", messages_length);
    guint32 size = int_length () + string_length (session_get_username (session)) + int_length ();
    for (int i = 0; i < messages_length; i++)
        size += int_length () + string_length (messages[i].msg);

    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_PROMPT_AUTHENTICATION, size, &offset);
    write_int (message, MAX_MESSAGE_LENGTH, priv->authentication_sequence_number, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, session_get_username (session), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, messages_length, &offset);
    int n_prompts = 0;
    for (int i = 0; i < messages_length; i++)
    {
        write_int (message, MAX_MESSAGE_LENGTH, messages[i].msg_style, &offset);
        write_string (message, MAX_MESSAGE_LENGTH, messages[i].msg, &offset);

        if (messages[i].msg_style == PAM_PROMPT_ECHO_OFF || messages[i].msg_style == PAM_PROMPT_ECHO_ON)
            n_prompts++;
    }
    write_message (greeter, message, offset);

    /* Continue immediately if nothing to respond with */
    // FIXME: Should probably give the greeter a chance to ack the message
    if (n_prompts == 0)
    {
        struct pam_response *response;
        response = calloc (messages_length, sizeof (struct pam_response));
        session_respond (priv->authentication_session, response);
        free (response);
    }
}

static void
send_end_authentication (Greeter *greeter, guint32 sequence_number, const gchar *username, int result)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_END_AUTHENTICATION, int_length () + string_length (username) + int_length (), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, sequence_number, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, username, &offset);
    write_int (message, MAX_MESSAGE_LENGTH, result, &offset);
    write_message (greeter, message, offset);
}

void
greeter_idle (Greeter *greeter)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_IDLE, 0, &offset);
    write_message (greeter, message, offset);
}

void
greeter_reset (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    g_return_if_fail (greeter != NULL);

    GHashTableIter iter;
    g_hash_table_iter_init (&iter, priv->hints);
    gpointer key, value;
    guint32 length = 0;
    while (g_hash_table_iter_next (&iter, &key, &value))
        length += string_length (key) + string_length (value);

    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_RESET, length, &offset);
    g_hash_table_iter_init (&iter, priv->hints);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        write_string (message, MAX_MESSAGE_LENGTH, key, &offset);
        write_string (message, MAX_MESSAGE_LENGTH, value, &offset);
    }
    write_message (greeter, message, offset);
}

static void
authentication_complete_cb (Session *session, Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    g_debug ("Authenticate result for user %s: %s", session_get_username (session), session_get_authentication_result_string (session));

    int result = session_get_authentication_result (session);
    if (session_get_is_authenticated (session))
    {
        if (session_get_user (session))
            g_debug ("User %s authorized", session_get_username (session));
        else
        {
            g_debug ("User %s authorized, but no account of that name exists", session_get_username (session));
            result = PAM_USER_UNKNOWN;
        }
    }

    send_end_authentication (greeter, priv->authentication_sequence_number, session_get_username (session), result);
}

static void
reset_session (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    g_free (priv->remote_session);
    priv->remote_session = NULL;
    if (priv->authentication_session)
    {
        g_signal_handlers_disconnect_matched (priv->authentication_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, greeter);
        session_stop (priv->authentication_session);
        g_clear_object (&priv->authentication_session);
    }

    priv->guest_account_authenticated = FALSE;
}

static void
handle_authenticate (Greeter *greeter, guint32 sequence_number, const gchar *username)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    if (username[0] == '\0')
    {
        g_debug ("Greeter start authentication");
        username = NULL;
    }
    else
        g_debug ("Greeter start authentication for %s", username);

    reset_session (greeter);

    if (priv->active_username)
        g_free (priv->active_username);
    priv->active_username = g_strdup (username);
    g_object_notify (G_OBJECT (greeter), GREETER_PROPERTY_ACTIVE_USERNAME);

    priv->authentication_sequence_number = sequence_number;
    g_signal_emit (greeter, signals[CREATE_SESSION], 0, &priv->authentication_session);
    if (!priv->authentication_session)
    {
        send_end_authentication (greeter, sequence_number, "", PAM_USER_UNKNOWN);
        return;
    }

    g_signal_connect (G_OBJECT (priv->authentication_session), SESSION_SIGNAL_GOT_MESSAGES, G_CALLBACK (pam_messages_cb), greeter);
    g_signal_connect (G_OBJECT (priv->authentication_session), SESSION_SIGNAL_AUTHENTICATION_COMPLETE, G_CALLBACK (authentication_complete_cb), greeter);

    /* Use non-interactive service for autologin user */
    const gchar *autologin_username = g_hash_table_lookup (priv->hints, "autologin-user");
    const gchar *service;
    gboolean is_interactive;
    if (autologin_username != NULL && g_strcmp0 (username, autologin_username) == 0)
    {
        service = priv->autologin_pam_service;
        is_interactive = FALSE;
    }
    else
    {
        service = priv->pam_service;
        is_interactive = TRUE;
    }

    /* Run the session process */
    session_set_pam_service (priv->authentication_session, service);
    session_set_username (priv->authentication_session, username);
    session_set_do_authenticate (priv->authentication_session, TRUE);
    session_set_is_interactive (priv->authentication_session, is_interactive);
    session_start (priv->authentication_session);
}

static void
handle_authenticate_as_guest (Greeter *greeter, guint32 sequence_number)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    g_debug ("Greeter start authentication for guest account");

    reset_session (greeter);

    if (!priv->allow_guest)
    {
        g_debug ("Guest account is disabled");
        send_end_authentication (greeter, sequence_number, "", PAM_USER_UNKNOWN);
        return;
    }

    priv->guest_account_authenticated = TRUE;
    send_end_authentication (greeter, sequence_number, "", PAM_SUCCESS);
}

static gchar *
get_remote_session_service (const gchar *session_name)
{
    /* Validate session name doesn't contain directory separators */
    for (const gchar *c = session_name; *c; c++)
    {
        if (*c == '/')
            return NULL;
    }

    /* Load the session file */
    g_autoptr(GKeyFile) session_desktop_file = g_key_file_new ();
    g_autofree gchar *filename = g_strdup_printf ("%s.desktop", session_name);
    g_autofree gchar *remote_sessions_dir = config_get_string (config_get_instance (), "LightDM", "remote-sessions-directory");
    g_autofree gchar *path = g_build_filename (remote_sessions_dir, filename, NULL);
    g_autoptr(GError) error = NULL;
    gboolean result = g_key_file_load_from_file (session_desktop_file, path, G_KEY_FILE_NONE, &error);
    if (error)
        g_debug ("Failed to load session file %s: %s", path, error->message);
    if (!result)
        return NULL;

    return g_key_file_get_string (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-PAM-Service", NULL);
}

static void
handle_authenticate_remote (Greeter *greeter, const gchar *session_name, const gchar *username, guint32 sequence_number)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    if (username[0] == '\0')
    {
        g_debug ("Greeter start authentication for remote session %s", session_name);
        username = NULL;
    }
    else
        g_debug ("Greeter start authentication for remote session %s as user %s", session_name, username);

    reset_session (greeter);

    g_autofree gchar *service = get_remote_session_service (session_name);
    if (!service)
    {
        send_end_authentication (greeter, sequence_number, "", PAM_SYSTEM_ERR);
        return;
    }

    priv->authentication_sequence_number = sequence_number;
    priv->remote_session = g_strdup (session_name);
    g_signal_emit (greeter, signals[CREATE_SESSION], 0, &priv->authentication_session);
    if (priv->authentication_session)
    {
        g_signal_connect (G_OBJECT (priv->authentication_session), SESSION_SIGNAL_GOT_MESSAGES, G_CALLBACK (pam_messages_cb), greeter);
        g_signal_connect (G_OBJECT (priv->authentication_session), SESSION_SIGNAL_AUTHENTICATION_COMPLETE, G_CALLBACK (authentication_complete_cb), greeter);

        /* Run the session process */
        session_set_pam_service (priv->authentication_session, service);
        session_set_username (priv->authentication_session, username);
        session_set_do_authenticate (priv->authentication_session, TRUE);
        session_set_is_interactive (priv->authentication_session, TRUE);
        session_set_is_guest (priv->authentication_session, TRUE);
        session_start (priv->authentication_session);
    }

    if (!priv->authentication_session)
    {
        send_end_authentication (greeter, sequence_number, "", PAM_USER_UNKNOWN);
        return;
    }
}

static void
handle_continue_authentication (Greeter *greeter, gchar **secrets)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    /* Not in authentication */
    if (priv->authentication_session == NULL)
        return;

    int messages_length = session_get_messages_length (priv->authentication_session);
    const struct pam_message *messages = session_get_messages (priv->authentication_session);

    /* Check correct number of responses */
    int n_prompts = 0;
    for (int i = 0; i < messages_length; i++)
    {
        int msg_style = messages[i].msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
            n_prompts++;
    }
    if (g_strv_length (secrets) != n_prompts)
    {
        session_respond_error (priv->authentication_session, PAM_CONV_ERR);
        return;
    }

    g_debug ("Continue authentication");

    /* Build response */
    struct pam_response *response = calloc (messages_length, sizeof (struct pam_response));
    for (int i = 0, j = 0; i < messages_length; i++)
    {
        int msg_style = messages[i].msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
        {
            size_t secret_length = strlen (secrets[j]) + 1;
            response[i].resp = secure_malloc (greeter, secret_length);
            memcpy (response[i].resp, secrets[j], secret_length); // FIXME: Need to convert from UTF-8
            j++;
        }
    }

    session_respond (priv->authentication_session, response);

    for (int i = 0; i < messages_length; i++)
        secure_free (greeter, response[i].resp);
    free (response);
}

static void
handle_cancel_authentication (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    /* Not in authentication */
    if (priv->authentication_session == NULL)
        return;

    g_debug ("Cancel authentication");
    reset_session (greeter);
}

static void
handle_start_session (Greeter *greeter, const gchar *session)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    if (strcmp (session, "") == 0)
        session = NULL;

    /* Use session type chosen in remote session */
    SessionType session_type = SESSION_TYPE_LOCAL;
    if (priv->remote_session)
    {
        session_type = SESSION_TYPE_REMOTE;
        session = priv->remote_session;
    }

    gboolean result;
    if (priv->guest_account_authenticated || session_get_is_authenticated (priv->authentication_session))
    {
        if (session)
            g_debug ("Greeter requests session %s", session);
        else
            g_debug ("Greeter requests default session");
        priv->start_session = TRUE;
        g_signal_emit (greeter, signals[START_SESSION], 0, session_type, session, &result);
    }
    else
    {
        g_debug ("Ignoring start session request, user is not authorized");
        result = FALSE;
    }

    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_SESSION_RESULT, int_length (), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, result ? 0 : 1, &offset);
    write_message (greeter, message, offset);
}

static void
handle_set_language (Greeter *greeter, const gchar *language)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    if (!priv->guest_account_authenticated && !session_get_is_authenticated (priv->authentication_session))
    {
        g_debug ("Ignoring set language request, user is not authorized");
        return;
    }

    // FIXME: Could use this
    if (priv->guest_account_authenticated)
    {
        g_debug ("Ignoring set language request for guest user");
        return;
    }

    g_debug ("Greeter sets language %s", language);
    User *user = session_get_user (priv->authentication_session);
    user_set_language (user, language);
}

static void
handle_ensure_shared_dir (Greeter *greeter, const gchar *username)
{
    g_debug ("Greeter requests data directory for user %s", username);

    g_autofree gchar *dir = shared_data_manager_ensure_user_dir (shared_data_manager_get_instance (), username);

    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_SHARED_DIR_RESULT, string_length (dir), &offset);
    write_string (message, MAX_MESSAGE_LENGTH, dir, &offset);
    write_message (greeter, message, offset);
}

static guint32
read_int (Greeter *greeter, gsize *offset)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    if (priv->n_read - *offset < sizeof (guint32))
    {
        g_warning ("Not enough space for int, need %zu, got %zu", sizeof (guint32), priv->n_read - *offset);
        return 0;
    }
    guint8 *buffer = priv->read_buffer + *offset;
    guint32 value = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
    *offset += int_length ();
    return value;
}

static int
get_message_length (Greeter *greeter)
{
    gsize offset;
    int payload_length;

    offset = int_length ();
    payload_length = read_int (greeter, &offset);

    if (HEADER_SIZE + payload_length < HEADER_SIZE)
    {
        g_warning ("Payload length of %u octets too long", payload_length);
        return HEADER_SIZE;
    }

    return HEADER_SIZE + payload_length;
}

static gchar *
read_string_full (Greeter *greeter, gsize *offset, void* (*alloc_fn)(size_t n))
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    guint32 length = read_int (greeter, offset);
    if (priv->n_read - *offset < length)
    {
        g_warning ("Not enough space for string, need %u, got %zu", length, priv->n_read - *offset);
        return g_strdup ("");
    }

    gchar *value = (*alloc_fn) (sizeof (gchar) * (length + 1));
    memcpy (value, priv->read_buffer + *offset, length);
    value[length] = '\0';
    *offset += length;

    return value;
}

static gchar *
read_string (Greeter *greeter, gsize *offset)
{
    return read_string_full (greeter, offset, g_malloc);
}

static gchar *
read_secret (Greeter *greeter, gsize *offset)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    if (priv->use_secure_memory)
        return read_string_full (greeter, offset, gcry_malloc_secure);
    else
        return read_string_full (greeter, offset, g_malloc);
}

static gboolean
read_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    Greeter *greeter = data;
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    if (condition == G_IO_HUP)
    {
        g_debug ("Greeter closed communication channel");
        priv->from_greeter_watch = 0;
        g_signal_emit (greeter, signals[DISCONNECTED], 0);
        return FALSE;
    }

    gsize n_to_read = HEADER_SIZE;
    if (priv->n_read >= HEADER_SIZE)
    {
        n_to_read = get_message_length (greeter);
        if (n_to_read <= HEADER_SIZE)
        {
            priv->from_greeter_watch = 0;
            return FALSE;
        }
    }

    gsize n_read;
    g_autoptr(GError) error = NULL;
    GIOStatus status = g_io_channel_read_chars (priv->from_greeter_channel,
                                                (gchar *) priv->read_buffer + priv->n_read,
                                                n_to_read - priv->n_read,
                                                &n_read,
                                                &error);
    if (error)
        g_warning ("Error reading from greeter: %s", error->message);
    if (status == G_IO_STATUS_EOF)
    {
        g_debug ("Greeter closed communication channel");
        priv->from_greeter_watch = 0;
        g_signal_emit (greeter, signals[DISCONNECTED], 0);
        return FALSE;
    }
    else if (status != G_IO_STATUS_NORMAL)
        return TRUE;

    priv->n_read += n_read;
    if (priv->n_read != n_to_read)
        return TRUE;

    /* If have header, rerun for content */
    if (priv->n_read == HEADER_SIZE)
    {
        n_to_read = get_message_length (greeter);
        if (n_to_read > HEADER_SIZE)
        {
            priv->read_buffer = secure_realloc (greeter, priv->read_buffer, n_to_read);
            read_cb (source, condition, greeter);
            return TRUE;
        }
    }

    gsize offset = 0;
    int id = read_int (greeter, &offset);
    int length = HEADER_SIZE + read_int (greeter, &offset);
    switch (id)
    {
    case GREETER_MESSAGE_CONNECT:
        {
            g_autofree gchar *version = read_string (greeter, &offset);
            gboolean resettable = FALSE;
            if (offset < length)
                resettable = read_int (greeter, &offset) != 0;
            guint32 api_version = 0;
            if (offset < length)
                api_version = read_int (greeter, &offset);
            handle_connect (greeter, version, resettable, api_version);
        }
        break;
    case GREETER_MESSAGE_AUTHENTICATE:
        {
            guint32 sequence_number = read_int (greeter, &offset);
            g_autofree gchar *username = read_string (greeter, &offset);
            handle_authenticate (greeter, sequence_number, username);
        }
        break;
    case GREETER_MESSAGE_AUTHENTICATE_AS_GUEST:
        {
            guint32 sequence_number = read_int (greeter, &offset);
            handle_authenticate_as_guest (greeter, sequence_number);
        }
        break;
    case GREETER_MESSAGE_AUTHENTICATE_REMOTE:
        {
            guint32 sequence_number = read_int (greeter, &offset);
            g_autofree gchar *session_name = read_string (greeter, &offset);
            g_autofree gchar *username = read_string (greeter, &offset);
            handle_authenticate_remote (greeter, session_name, username, sequence_number);
        }
        break;
    case GREETER_MESSAGE_CONTINUE_AUTHENTICATION:
        {
            guint32 n_secrets = read_int (greeter, &offset);
            guint32 max_secrets = (G_MAXUINT32 - 1) / sizeof (gchar *);
            if (n_secrets > max_secrets)
            {
                g_warning ("Array length of %u elements too long", n_secrets);
                priv->from_greeter_watch = 0;
                return FALSE;
            }
            gchar **secrets = g_malloc (sizeof (gchar *) * (n_secrets + 1));
            guint32 i;
            for (i = 0; i < n_secrets; i++)
                secrets[i] = read_secret (greeter, &offset);
            secrets[i] = NULL;
            handle_continue_authentication (greeter, secrets);
            secure_freev (greeter, secrets);
        }
        break;
    case GREETER_MESSAGE_CANCEL_AUTHENTICATION:
        handle_cancel_authentication (greeter);
        break;
    case GREETER_MESSAGE_START_SESSION:
        {
            g_autofree gchar *session_name = read_string (greeter, &offset);
            handle_start_session (greeter, session_name);
        }
        break;
    case GREETER_MESSAGE_SET_LANGUAGE:
        {
            g_autofree gchar *language = read_string (greeter, &offset);
            handle_set_language (greeter, language);
        }
        break;
    case GREETER_MESSAGE_ENSURE_SHARED_DIR:
        {
            g_autofree gchar *username = read_string (greeter, &offset);
            handle_ensure_shared_dir (greeter, username);
        }
        break;
    default:
        g_warning ("Unknown message from greeter: %d", id);
        break;
    }

    priv->n_read = 0;

    return TRUE;
}

gboolean
greeter_get_guest_authenticated (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    g_return_val_if_fail (greeter != NULL, FALSE);
    return priv->guest_account_authenticated;
}

Session *
greeter_take_authentication_session (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    g_return_val_if_fail (greeter != NULL, NULL);

    Session *session = priv->authentication_session;
    if (priv->authentication_session)
        g_signal_handlers_disconnect_matched (priv->authentication_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, greeter);
    priv->authentication_session = NULL;

    return session;
}

gboolean
greeter_get_resettable (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    g_return_val_if_fail (greeter != NULL, FALSE);
    return priv->resettable;
}

gboolean
greeter_get_start_session (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    g_return_val_if_fail (greeter != NULL, FALSE);
    return priv->start_session;
}

const gchar *
greeter_get_active_username (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);
    g_return_val_if_fail (greeter != NULL, NULL);
    return priv->active_username;
}

static Session *
greeter_real_create_session (Greeter *greeter)
{
    return NULL;
}

static gboolean
greeter_real_start_session (Greeter *greeter, SessionType type, const gchar *session)
{
    return FALSE;
}

static void
greeter_init (Greeter *greeter)
{
    GreeterPrivate *priv = greeter_get_instance_private (greeter);

    priv->read_buffer = secure_malloc (greeter, HEADER_SIZE);
    priv->hints = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    priv->use_secure_memory = config_get_boolean (config_get_instance (), "LightDM", "lock-memory");
    priv->to_greeter_input = -1;
    priv->from_greeter_output = -1;
}

static void
greeter_finalize (GObject *object)
{
    Greeter *self = GREETER (object);
    GreeterPrivate *priv = greeter_get_instance_private (self);

    g_clear_pointer (&priv->pam_service, g_free);
    g_clear_pointer (&priv->autologin_pam_service, g_free);
    secure_free (self, priv->read_buffer);
    g_hash_table_unref (priv->hints);
    g_clear_pointer (&priv->remote_session, g_free);
    g_clear_pointer (&priv->active_username, g_free);
    if (priv->authentication_session)
    {
        g_signal_handlers_disconnect_matched (priv->authentication_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
        g_object_unref (priv->authentication_session);
    }
    close (priv->to_greeter_input);
    close (priv->from_greeter_output);
    if (priv->to_greeter_channel)
        g_io_channel_unref (priv->to_greeter_channel);
    if (priv->from_greeter_channel)
        g_io_channel_unref (priv->from_greeter_channel);
    if (priv->from_greeter_watch)
        g_source_remove (priv->from_greeter_watch);

    G_OBJECT_CLASS (greeter_parent_class)->finalize (object);
}

static void
greeter_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
greeter_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
    Greeter *greeter = GREETER (object);

    switch (prop_id) {
    case PROP_ACTIVE_USERNAME:
        g_value_set_string (value, greeter_get_active_username (greeter));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
greeter_class_init (GreeterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->create_session = greeter_real_create_session;
    klass->start_session = greeter_real_start_session;
    object_class->finalize = greeter_finalize;
    object_class->get_property = greeter_get_property;
    object_class->set_property = greeter_set_property;

    signals[CONNECTED] =
        g_signal_new (GREETER_SIGNAL_CONNECTED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, connected),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[DISCONNECTED] =
        g_signal_new (GREETER_SIGNAL_DISCONNECTED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, disconnected),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[CREATE_SESSION] =
        g_signal_new (GREETER_SIGNAL_CREATE_SESSION,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, create_session),
                      g_signal_accumulator_first_wins,
                      NULL,
                      NULL,
                      SESSION_TYPE, 0);

    signals[START_SESSION] =
        g_signal_new (GREETER_SIGNAL_START_SESSION,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, start_session),
                      g_signal_accumulator_true_handled,
                      NULL,
                      NULL,
                      G_TYPE_BOOLEAN, 2, G_TYPE_INT, G_TYPE_STRING);

    g_object_class_install_property (object_class,
                                     PROP_ACTIVE_USERNAME,
                                     g_param_spec_string (GREETER_PROPERTY_ACTIVE_USERNAME,
                                                          GREETER_PROPERTY_ACTIVE_USERNAME,
                                                          "Active username",
                                                          NULL,
                                                          G_PARAM_READABLE));
}
