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
#include <fcntl.h>
#include <gcrypt.h>

#include "greeter.h"
#include "configuration.h"
#include "shared-data-manager.h"

enum {
    PROP_0,
    PROP_ACTIVE_USERNAME,
};

enum {
    CONNECTED,
    CREATE_SESSION,
    START_SESSION,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct GreeterPrivate
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
};

G_DEFINE_TYPE (Greeter, greeter, SESSION_TYPE);

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
} ServerMessage;

static gboolean read_cb (GIOChannel *source, GIOCondition condition, gpointer data);

Greeter *
greeter_new (void)
{
    return g_object_new (GREETER_TYPE, NULL);
}

void
greeter_set_pam_services (Greeter *greeter, const gchar *pam_service, const gchar *autologin_pam_service)
{
    g_free (greeter->priv->pam_service);
    greeter->priv->pam_service = g_strdup (pam_service);
    g_free (greeter->priv->autologin_pam_service);
    greeter->priv->autologin_pam_service = g_strdup (autologin_pam_service);
}

void
greeter_set_allow_guest (Greeter *greeter, gboolean allow_guest)
{
    greeter->priv->allow_guest = allow_guest;
}

void
greeter_clear_hints (Greeter *greeter)
{
    g_hash_table_remove_all (greeter->priv->hints);
}

void
greeter_set_hint (Greeter *greeter, const gchar *name, const gchar *value)
{
    g_hash_table_insert (greeter->priv->hints, g_strdup (name), g_strdup (value));
}

static void *
secure_malloc (Greeter *greeter, size_t n)
{
    if (greeter->priv->use_secure_memory)
        return gcry_malloc_secure (n);
    else
        return g_malloc (n);
}

static void *
secure_realloc (Greeter *greeter, void *ptr, size_t n)
{
    if (greeter->priv->use_secure_memory)
        return gcry_realloc (ptr, n);
    else
        return g_realloc (ptr, n);
}

static void
secure_free (Greeter *greeter, void *ptr)
{
    if (greeter->priv->use_secure_memory)
        return gcry_free (ptr);
    else
        return g_free (ptr);
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
    gchar *data;
    gsize data_length;
    GError *error = NULL;

    data = (gchar *) message;
    data_length = message_length;
    while (data_length > 0)
    {
        GIOStatus status;
        gsize n_written;

        status = g_io_channel_write_chars (greeter->priv->to_greeter_channel, data, data_length, &n_written, &error);
        if (error)
            l_warning (greeter, "Error writing to greeter: %s", error->message);
        g_clear_error (&error);
        if (status != G_IO_STATUS_NORMAL)
            return;
        data_length -= n_written;
        data += n_written;
    }

    g_io_channel_flush (greeter->priv->to_greeter_channel, &error);
    if (error)
        l_warning (greeter, "Failed to flush data to greeter: %s", error->message);
    g_clear_error (&error);
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
handle_connect (Greeter *greeter, const gchar *version, gboolean resettable)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    guint32 length;
    GHashTableIter iter;
    gpointer key, value;

    l_debug (greeter, "Greeter connected version=%s resettable=%s", version, resettable ? "true" : "false");

    greeter->priv->resettable = resettable;

    length = string_length (VERSION);
    g_hash_table_iter_init (&iter, greeter->priv->hints);
    while (g_hash_table_iter_next (&iter, &key, &value))
        length += string_length (key) + string_length (value);

    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_CONNECTED, length, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, VERSION, &offset);
    g_hash_table_iter_init (&iter, greeter->priv->hints);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        write_string (message, MAX_MESSAGE_LENGTH, key, &offset);
        write_string (message, MAX_MESSAGE_LENGTH, value, &offset);
    }
    write_message (greeter, message, offset);

    g_signal_emit (greeter, signals[CONNECTED], 0);
}

static void
pam_messages_cb (Session *session, Greeter *greeter)
{
    int i;
    guint32 size;
    guint8 message[MAX_MESSAGE_LENGTH];
    const struct pam_message *messages;
    int messages_length;
    gsize offset = 0;
    int n_prompts = 0;

    messages = session_get_messages (session);
    messages_length = session_get_messages_length (session);

    /* Respond to d-bus query with messages */
    l_debug (greeter, "Prompt greeter with %d message(s)", messages_length);
    size = int_length () + string_length (session_get_username (session)) + int_length ();
    for (i = 0; i < messages_length; i++)
        size += int_length () + string_length (messages[i].msg);

    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_PROMPT_AUTHENTICATION, size, &offset);
    write_int (message, MAX_MESSAGE_LENGTH, greeter->priv->authentication_sequence_number, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, session_get_username (session), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, messages_length, &offset);
    for (i = 0; i < messages_length; i++)
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
        session_respond (greeter->priv->authentication_session, response);
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
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    guint32 length = 0;
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, greeter->priv->hints);
    while (g_hash_table_iter_next (&iter, &key, &value))
        length += string_length (key) + string_length (value);

    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_RESET, length, &offset);
    g_hash_table_iter_init (&iter, greeter->priv->hints);
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
    int result;

    l_debug (greeter, "Authenticate result for user %s: %s", session_get_username (session), session_get_authentication_result_string (session));

    result = session_get_authentication_result (session);
    if (session_get_is_authenticated (session))
    {
        if (session_get_user (session))
            l_debug (greeter, "User %s authorized", session_get_username (session));
        else
        {
            l_debug (greeter, "User %s authorized, but no account of that name exists", session_get_username (session));
            result = PAM_USER_UNKNOWN;
        }
    }

    send_end_authentication (greeter, greeter->priv->authentication_sequence_number, session_get_username (session), result);
}

static void
reset_session (Greeter *greeter)
{
    g_free (greeter->priv->remote_session);
    greeter->priv->remote_session = NULL;
    if (greeter->priv->authentication_session)
    {
        g_signal_handlers_disconnect_matched (greeter->priv->authentication_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, greeter);
        session_stop (greeter->priv->authentication_session);
        g_object_unref (greeter->priv->authentication_session);
        greeter->priv->authentication_session = NULL;
    }

    greeter->priv->guest_account_authenticated = FALSE;
}

static void
handle_login (Greeter *greeter, guint32 sequence_number, const gchar *username)
{
    const gchar *autologin_username, *service;
    gboolean is_interactive;

    if (username[0] == '\0')
    {
        l_debug (greeter, "Greeter start authentication");
        username = NULL;
    }
    else
        l_debug (greeter, "Greeter start authentication for %s", username);

    reset_session (greeter);

    if (greeter->priv->active_username)
        g_free (greeter->priv->active_username);
    greeter->priv->active_username = g_strdup (username);
    g_object_notify (G_OBJECT (greeter), "active-username");

    greeter->priv->authentication_sequence_number = sequence_number;
    g_signal_emit (greeter, signals[CREATE_SESSION], 0, &greeter->priv->authentication_session);
    if (!greeter->priv->authentication_session)
    {
        send_end_authentication (greeter, sequence_number, "", PAM_USER_UNKNOWN);
        return;
    }

    g_signal_connect (G_OBJECT (greeter->priv->authentication_session), "got-messages", G_CALLBACK (pam_messages_cb), greeter);
    g_signal_connect (G_OBJECT (greeter->priv->authentication_session), "authentication-complete", G_CALLBACK (authentication_complete_cb), greeter);

    /* Use non-interactive service for autologin user */
    autologin_username = g_hash_table_lookup (greeter->priv->hints, "autologin-user");
    if (autologin_username != NULL && g_strcmp0 (username, autologin_username) == 0)
    {
        service = greeter->priv->autologin_pam_service;
        is_interactive = FALSE;
    }
    else
    {
        service = greeter->priv->pam_service;
        is_interactive = TRUE;
    }

    /* Run the session process */
    session_set_pam_service (greeter->priv->authentication_session, service);
    session_set_username (greeter->priv->authentication_session, username);
    session_set_do_authenticate (greeter->priv->authentication_session, TRUE);
    session_set_is_interactive (greeter->priv->authentication_session, is_interactive);
    session_start (greeter->priv->authentication_session);
}

static void
handle_login_as_guest (Greeter *greeter, guint32 sequence_number)
{
    l_debug (greeter, "Greeter start authentication for guest account");

    reset_session (greeter);

    if (!greeter->priv->allow_guest)
    {
        l_debug (greeter, "Guest account is disabled");
        send_end_authentication (greeter, sequence_number, "", PAM_USER_UNKNOWN);
        return;
    }

    greeter->priv->guest_account_authenticated = TRUE;
    send_end_authentication (greeter, sequence_number, "", PAM_SUCCESS);
}

static gchar *
get_remote_session_service (const gchar *session_name)
{
    GKeyFile *session_desktop_file;
    gboolean result;
    const gchar *c;
    gchar *remote_sessions_dir, *filename, *path, *service = NULL;
    GError *error = NULL;

    /* Validate session name doesn't contain directory separators */
    for (c = session_name; *c; c++)
    {
        if (*c == '/')
            return NULL;
    }

    /* Load the session file */
    session_desktop_file = g_key_file_new ();
    filename = g_strdup_printf ("%s.desktop", session_name);
    remote_sessions_dir = config_get_string (config_get_instance (), "LightDM", "remote-sessions-directory");
    path = g_build_filename (remote_sessions_dir, filename, NULL);
    g_free (remote_sessions_dir);
    g_free (filename);
    result = g_key_file_load_from_file (session_desktop_file, path, G_KEY_FILE_NONE, &error);
    if (error)
        g_debug ("Failed to load session file %s: %s", path, error->message);
    g_free (path);
    g_clear_error (&error);
    if (result)
        service = g_key_file_get_string (session_desktop_file, G_KEY_FILE_DESKTOP_GROUP, "X-LightDM-PAM-Service", NULL);
    g_key_file_free (session_desktop_file);

    return service;
}

static void
handle_login_remote (Greeter *greeter, const gchar *session_name, const gchar *username, guint32 sequence_number)
{
    gchar *service;

    if (username[0] == '\0')
    {
        l_debug (greeter, "Greeter start authentication for remote session %s", session_name);
        username = NULL;
    }
    else
        l_debug (greeter, "Greeter start authentication for remote session %s as user %s", session_name, username);

    reset_session (greeter);

    service = get_remote_session_service (session_name);
    if (!service)
    {
        send_end_authentication (greeter, sequence_number, "", PAM_SYSTEM_ERR);
        return;
    }

    greeter->priv->authentication_sequence_number = sequence_number;
    greeter->priv->remote_session = g_strdup (session_name);
    g_signal_emit (greeter, signals[CREATE_SESSION], 0, &greeter->priv->authentication_session);
    if (greeter->priv->authentication_session)
    {
        g_signal_connect (G_OBJECT (greeter->priv->authentication_session), "got-messages", G_CALLBACK (pam_messages_cb), greeter);
        g_signal_connect (G_OBJECT (greeter->priv->authentication_session), "authentication-complete", G_CALLBACK (authentication_complete_cb), greeter);

        /* Run the session process */
        session_set_pam_service (greeter->priv->authentication_session, service);
        session_set_username (greeter->priv->authentication_session, username);
        session_set_do_authenticate (greeter->priv->authentication_session, TRUE);
        session_set_is_interactive (greeter->priv->authentication_session, TRUE);
        session_set_is_guest (greeter->priv->authentication_session, TRUE);
        session_start (greeter->priv->authentication_session);
    }

    g_free (service);

    if (!greeter->priv->authentication_session)
    {
        send_end_authentication (greeter, sequence_number, "", PAM_USER_UNKNOWN);
        return;
    }
}

static void
handle_continue_authentication (Greeter *greeter, gchar **secrets)
{
    int messages_length;
    const struct pam_message *messages;
    struct pam_response *response;
    int i, j, n_prompts = 0;

    /* Not in authentication */
    if (greeter->priv->authentication_session == NULL)
        return;

    messages_length = session_get_messages_length (greeter->priv->authentication_session);
    messages = session_get_messages (greeter->priv->authentication_session);

    /* Check correct number of responses */
    for (i = 0; i < messages_length; i++)
    {
        int msg_style = messages[i].msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
            n_prompts++;
    }
    if (g_strv_length (secrets) != n_prompts)
    {
        session_respond_error (greeter->priv->authentication_session, PAM_CONV_ERR);
        return;
    }

    l_debug (greeter, "Continue authentication");

    /* Build response */
    response = calloc (messages_length, sizeof (struct pam_response));
    for (i = 0, j = 0; i < messages_length; i++)
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

    session_respond (greeter->priv->authentication_session, response);

    for (i = 0; i < messages_length; i++)
        secure_free (greeter, response[i].resp);
    free (response);
}

static void
handle_cancel_authentication (Greeter *greeter)
{
    /* Not in authentication */
    if (greeter->priv->authentication_session == NULL)
        return;

    l_debug (greeter, "Cancel authentication");
    reset_session (greeter);
}

static void
handle_start_session (Greeter *greeter, const gchar *session)
{
    gboolean result;
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    SessionType session_type = SESSION_TYPE_LOCAL;

    if (strcmp (session, "") == 0)
        session = NULL;

    /* Use session type chosen in remote session */
    if (greeter->priv->remote_session)
    {
        session_type = SESSION_TYPE_REMOTE;
        session = greeter->priv->remote_session;
    }

    if (greeter->priv->guest_account_authenticated || session_get_is_authenticated (greeter->priv->authentication_session))
    {
        if (session)
            l_debug (greeter, "Greeter requests session %s", session);
        else
            l_debug (greeter, "Greeter requests default session");
        greeter->priv->start_session = TRUE;
        g_signal_emit (greeter, signals[START_SESSION], 0, session_type, session, &result);
    }
    else
    {
        l_debug (greeter, "Ignoring start session request, user is not authorized");
        result = FALSE;
    }

    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_SESSION_RESULT, int_length (), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, result ? 0 : 1, &offset);
    write_message (greeter, message, offset);
}

static void
handle_set_language (Greeter *greeter, const gchar *language)
{
    User *user;

    if (!greeter->priv->guest_account_authenticated && !session_get_is_authenticated (greeter->priv->authentication_session))
    {
        l_debug (greeter, "Ignoring set language request, user is not authorized");
        return;
    }

    // FIXME: Could use this
    if (greeter->priv->guest_account_authenticated)
    {
        l_debug (greeter, "Ignoring set language request for guest user");
        return;
    }

    l_debug (greeter, "Greeter sets language %s", language);
    user = session_get_user (greeter->priv->authentication_session);
    user_set_language (user, language);
}

static void
handle_ensure_shared_dir (Greeter *greeter, const gchar *username)
{
    gchar *dir;
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    l_debug (greeter, "Greeter requests data directory for user %s", username);

    dir = shared_data_manager_ensure_user_dir (shared_data_manager_get_instance (), username);

    write_header (message, MAX_MESSAGE_LENGTH, SERVER_MESSAGE_SHARED_DIR_RESULT, string_length (dir), &offset);
    write_string (message, MAX_MESSAGE_LENGTH, dir, &offset);
    write_message (greeter, message, offset);

    g_free (dir);
}

static guint32
read_int (Greeter *greeter, gsize *offset)
{
    guint32 value;
    guint8 *buffer;
    if (greeter->priv->n_read - *offset < sizeof (guint32))
    {
        l_warning (greeter, "Not enough space for int, need %zu, got %zu", sizeof (guint32), greeter->priv->n_read - *offset);
        return 0;
    }
    buffer = greeter->priv->read_buffer + *offset;
    value = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
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
        l_warning (greeter, "Payload length of %u octets too long", payload_length);
        return HEADER_SIZE;
    }

    return HEADER_SIZE + payload_length;
}

static gchar *
read_string_full (Greeter *greeter, gsize *offset, void* (*alloc_fn)(size_t n))
{
    guint32 length;
    gchar *value;

    length = read_int (greeter, offset);
    if (greeter->priv->n_read - *offset < length)
    {
        l_warning (greeter, "Not enough space for string, need %u, got %zu", length, greeter->priv->n_read - *offset);
        return g_strdup ("");
    }

    value = (*alloc_fn) (sizeof (gchar) * (length + 1));
    memcpy (value, greeter->priv->read_buffer + *offset, length);
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
    if (greeter->priv->use_secure_memory)
        return read_string_full (greeter, offset, gcry_malloc_secure);
    else
        return read_string_full (greeter, offset, g_malloc);
}

static gboolean
read_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
    Greeter *greeter = data;
    gsize n_to_read, n_read, offset;
    GIOStatus status;
    int id, length, i;
    guint32 sequence_number, n_secrets, max_secrets;
    gchar *version, *username, *session_name, *language;
    gchar **secrets;
    gboolean resettable = FALSE;
    GError *error = NULL;

    if (condition == G_IO_HUP)
    {
        l_debug (greeter, "Greeter closed communication channel");
        greeter->priv->from_greeter_watch = 0;
        return FALSE;
    }

    n_to_read = HEADER_SIZE;
    if (greeter->priv->n_read >= HEADER_SIZE)
    {
        n_to_read = get_message_length (greeter);
        if (n_to_read <= HEADER_SIZE)
        {
            greeter->priv->from_greeter_watch = 0;
            return FALSE;
        }
    }

    status = g_io_channel_read_chars (greeter->priv->from_greeter_channel,
                                      (gchar *) greeter->priv->read_buffer + greeter->priv->n_read,
                                      n_to_read - greeter->priv->n_read,
                                      &n_read,
                                      &error);
    if (error)
        l_warning (greeter, "Error reading from greeter: %s", error->message);
    g_clear_error (&error);
    if (status != G_IO_STATUS_NORMAL)
        return TRUE;

    greeter->priv->n_read += n_read;
    if (greeter->priv->n_read != n_to_read)
        return TRUE;

    /* If have header, rerun for content */
    if (greeter->priv->n_read == HEADER_SIZE)
    {
        n_to_read = get_message_length (greeter);
        if (n_to_read > HEADER_SIZE)
        {
            greeter->priv->read_buffer = secure_realloc (greeter, greeter->priv->read_buffer, n_to_read);
            read_cb (source, condition, greeter);
            return TRUE;
        }
    }

    offset = 0;
    id = read_int (greeter, &offset);
    length = HEADER_SIZE + read_int (greeter, &offset);
    switch (id)
    {
    case GREETER_MESSAGE_CONNECT:
        version = read_string (greeter, &offset);
        if (offset < length)
            resettable = read_int (greeter, &offset) != 0;
        handle_connect (greeter, version, resettable);
        g_free (version);
        break;
    case GREETER_MESSAGE_AUTHENTICATE:
        sequence_number = read_int (greeter, &offset);
        username = read_string (greeter, &offset);
        handle_login (greeter, sequence_number, username);
        g_free (username);
        break;
    case GREETER_MESSAGE_AUTHENTICATE_AS_GUEST:
        sequence_number = read_int (greeter, &offset);
        handle_login_as_guest (greeter, sequence_number);
        break;
    case GREETER_MESSAGE_AUTHENTICATE_REMOTE:
        sequence_number = read_int (greeter, &offset);
        session_name = read_string (greeter, &offset);
        username = read_string (greeter, &offset);
        handle_login_remote (greeter, session_name, username, sequence_number);
        break;
    case GREETER_MESSAGE_CONTINUE_AUTHENTICATION:
        n_secrets = read_int (greeter, &offset);
        max_secrets = (G_MAXUINT32 - 1) / sizeof (gchar *);
        if (n_secrets > max_secrets)
        {
            l_warning (greeter, "Array length of %u elements too long", n_secrets);
            greeter->priv->from_greeter_watch = 0;
            return FALSE;
        }
        secrets = g_malloc (sizeof (gchar *) * (n_secrets + 1));
        for (i = 0; i < n_secrets; i++)
            secrets[i] = read_secret (greeter, &offset);
        secrets[i] = NULL;
        handle_continue_authentication (greeter, secrets);
        for (i = 0; i < n_secrets; i++)
            secure_free (greeter, secrets[i]);
        g_free (secrets);
        break;
    case GREETER_MESSAGE_CANCEL_AUTHENTICATION:
        handle_cancel_authentication (greeter);
        break;
    case GREETER_MESSAGE_START_SESSION:
        session_name = read_string (greeter, &offset);
        handle_start_session (greeter, session_name);
        g_free (session_name);
        break;
    case GREETER_MESSAGE_SET_LANGUAGE:
        language = read_string (greeter, &offset);
        handle_set_language (greeter, language);
        g_free (language);
        break;
    case GREETER_MESSAGE_ENSURE_SHARED_DIR:
        username = read_string (greeter, &offset);
        handle_ensure_shared_dir (greeter, username);
        g_free (username);
        break;
    default:
        l_warning (greeter, "Unknown message from greeter: %d", id);
        break;
    }

    greeter->priv->n_read = 0;

    return TRUE;
}

gboolean
greeter_get_guest_authenticated (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, FALSE);
    return greeter->priv->guest_account_authenticated;
}

Session *
greeter_get_authentication_session (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->authentication_session;
}

gboolean
greeter_get_resettable (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, FALSE);
    return greeter->priv->resettable;
}

gboolean
greeter_get_start_session (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, FALSE);
    return greeter->priv->start_session;
}

const gchar *
greeter_get_active_username (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->active_username;
}

static gboolean
greeter_start (Session *session)
{
    Greeter *greeter = GREETER (session);
    int to_greeter_pipe[2], from_greeter_pipe[2];
    int to_greeter_output, from_greeter_input;
    gboolean result = FALSE;
    gchar *value;
    GError *error = NULL;

    /* Create a pipe to talk with the greeter */
    if (pipe (to_greeter_pipe) != 0 || pipe (from_greeter_pipe) != 0)
    {
        g_warning ("Failed to create pipes: %s", strerror (errno));
        return FALSE;
    }
    to_greeter_output = to_greeter_pipe[0];
    greeter->priv->to_greeter_input = to_greeter_pipe[1];
    greeter->priv->to_greeter_channel = g_io_channel_unix_new (greeter->priv->to_greeter_input);
    g_io_channel_set_encoding (greeter->priv->to_greeter_channel, NULL, &error);
    if (error)
        g_warning ("Failed to set encoding on to greeter channel to binary: %s\n", error->message);
    g_clear_error (&error);
    greeter->priv->from_greeter_output = from_greeter_pipe[0];
    from_greeter_input = from_greeter_pipe[1];
    greeter->priv->from_greeter_channel = g_io_channel_unix_new (greeter->priv->from_greeter_output);
    g_io_channel_set_encoding (greeter->priv->from_greeter_channel, NULL, &error);
    if (error)
        g_warning ("Failed to set encoding on from greeter channel to binary: %s\n", error->message);
    g_clear_error (&error);
    g_io_channel_set_buffered (greeter->priv->from_greeter_channel, FALSE);
    greeter->priv->from_greeter_watch = g_io_add_watch (greeter->priv->from_greeter_channel, G_IO_IN | G_IO_HUP, read_cb, greeter);

    /* Let the greeter session know how to communicate with the daemon */
    value = g_strdup_printf ("%d", from_greeter_input);
    session_set_env (SESSION (greeter), "LIGHTDM_TO_SERVER_FD", value);
    g_free (value);
    value = g_strdup_printf ("%d", to_greeter_output);
    session_set_env (SESSION (greeter), "LIGHTDM_FROM_SERVER_FD", value);
    g_free (value);

    /* Don't allow the daemon end of the pipes to be accessed in child processes */
    fcntl (greeter->priv->to_greeter_input, F_SETFD, FD_CLOEXEC);
    fcntl (greeter->priv->from_greeter_output, F_SETFD, FD_CLOEXEC);

    result = SESSION_CLASS (greeter_parent_class)->start (session);

    /* Close the session ends of the pipe */
    close (to_greeter_output);
    close (from_greeter_input);

    return result;
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
greeter_stop (Session *session)
{
    Greeter *greeter = GREETER (session);

    /* Stop any events occurring after we've stopped */
    if (greeter->priv->authentication_session)
        g_signal_handlers_disconnect_matched (greeter->priv->authentication_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, greeter);

    SESSION_CLASS (greeter_parent_class)->stop (session);
}

static void
greeter_init (Greeter *greeter)
{
    greeter->priv = G_TYPE_INSTANCE_GET_PRIVATE (greeter, GREETER_TYPE, GreeterPrivate);
    greeter->priv->read_buffer = secure_malloc (greeter, HEADER_SIZE);
    greeter->priv->hints = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    greeter->priv->use_secure_memory = config_get_boolean (config_get_instance (), "LightDM", "lock-memory");
    greeter->priv->to_greeter_input = -1;
    greeter->priv->from_greeter_output = -1;
}

static void
greeter_finalize (GObject *object)
{
    Greeter *self;

    self = GREETER (object);

    g_free (self->priv->pam_service);
    g_free (self->priv->autologin_pam_service);
    secure_free (self, self->priv->read_buffer);
    g_hash_table_unref (self->priv->hints);
    g_free (self->priv->remote_session);
    g_free (self->priv->active_username);
    if (self->priv->authentication_session)
    {
        g_signal_handlers_disconnect_matched (self->priv->authentication_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
        g_object_unref (self->priv->authentication_session);
    }
    close (self->priv->to_greeter_input);
    close (self->priv->from_greeter_output);
    if (self->priv->to_greeter_channel)
        g_io_channel_unref (self->priv->to_greeter_channel);
    if (self->priv->from_greeter_channel)
        g_io_channel_unref (self->priv->from_greeter_channel);
    if (self->priv->from_greeter_watch)
        g_source_remove (self->priv->from_greeter_watch);

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
    SessionClass *session_class = SESSION_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->create_session = greeter_real_create_session;
    klass->start_session = greeter_real_start_session;
    session_class->start = greeter_start;
    session_class->stop = greeter_stop;
    object_class->finalize = greeter_finalize;
    object_class->get_property = greeter_get_property;
    object_class->set_property = greeter_set_property;

    signals[CONNECTED] =
        g_signal_new ("connected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, connected),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[CREATE_SESSION] =
        g_signal_new ("create-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, create_session),
                      g_signal_accumulator_first_wins,
                      NULL,
                      NULL,
                      SESSION_TYPE, 0);

    signals[START_SESSION] =
        g_signal_new ("start-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, start_session),
                      g_signal_accumulator_true_handled,
                      NULL,
                      NULL,
                      G_TYPE_BOOLEAN, 2, G_TYPE_INT, G_TYPE_STRING);

    g_object_class_install_property (object_class,
                                     PROP_ACTIVE_USERNAME,
                                     g_param_spec_string ("active-username",
                                                          "active-username",
                                                          "Active username",
                                                          NULL,
                                                          G_PARAM_READABLE));

    g_type_class_add_private (klass, sizeof (GreeterPrivate));
}
