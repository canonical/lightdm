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
#include "ldm-marshal.h"
#include "configuration.h"

enum {
    CONNECTED,
    START_AUTHENTICATION,
    START_SESSION,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct GreeterPrivate
{
    /* Session running on */
    Session *session;

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

    /* PAM session being constructed by the greeter */
    Session *authentication_session;

    /* TRUE if a user has been authenticated and the session requested to start */
    gboolean start_session;

    /* TRUE if can log into guest accounts */
    gboolean allow_guest;

    /* TRUE if logging into guest session */
    gboolean guest_account_authenticated;

    /* Communication channels to communicate with */
    GIOChannel *to_greeter_channel;
    GIOChannel *from_greeter_channel;
};

G_DEFINE_TYPE (Greeter, greeter, G_TYPE_OBJECT);

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
    GREETER_MESSAGE_AUTHENTICATE_REMOTE
} GreeterMessage;

/* Messages from the server to the greeter */
typedef enum
{
    SERVER_MESSAGE_CONNECTED = 0,
    SERVER_MESSAGE_PROMPT_AUTHENTICATION,
    SERVER_MESSAGE_END_AUTHENTICATION,
    SERVER_MESSAGE_SESSION_RESULT
} ServerMessage;

static gboolean read_cb (GIOChannel *source, GIOCondition condition, gpointer data);

Greeter *
greeter_new (Session *session, const gchar *pam_service, const gchar *autologin_pam_service)
{
    Greeter *greeter;

    greeter = g_object_new (GREETER_TYPE, NULL);
    greeter->priv->session = g_object_ref (session);
    greeter->priv->pam_service = g_strdup (pam_service);
    greeter->priv->autologin_pam_service = g_strdup (autologin_pam_service);
    greeter->priv->use_secure_memory = config_get_boolean (config_get_instance (), "LightDM", "lock-memory");

    return greeter;
}

void
greeter_set_allow_guest (Greeter *greeter, gboolean allow_guest)
{
    greeter->priv->allow_guest = allow_guest;
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
int_length ()
{
    return 4;
}

#define HEADER_SIZE (sizeof (guint32) * 2)
#define MAX_MESSAGE_LENGTH 1024

static void
write_message (Greeter *greeter, guint8 *message, gsize message_length)
{
    GError *error = NULL;

    g_io_channel_write_chars (greeter->priv->to_greeter_channel, (gchar *) message, message_length, NULL, &error);
    if (error)
        g_warning ("Error writing to greeter: %s", error->message);
    g_clear_error (&error);
    g_io_channel_flush (greeter->priv->to_greeter_channel, NULL);
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
handle_connect (Greeter *greeter, const gchar *version)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;
    guint32 length;
    GHashTableIter iter;
    gpointer key, value;

    g_debug ("Greeter connected version=%s", version);

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
    g_debug ("Prompt greeter with %d message(s)", messages_length);
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

static void
authentication_complete_cb (Session *session, Greeter *greeter)
{
    int result;

    g_debug ("Authenticate result for user %s: %s", session_get_username (session), session_get_authentication_result_string (session));

    result = session_get_authentication_result (session);
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
        g_debug ("Greeter start authentication");
        username = NULL;
    }
    else
        g_debug ("Greeter start authentication for %s", username);

    reset_session (greeter);

    greeter->priv->authentication_sequence_number = sequence_number;
    g_signal_emit (greeter, signals[START_AUTHENTICATION], 0, username, &greeter->priv->authentication_session);
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
    session_start (greeter->priv->authentication_session, service, username, TRUE, is_interactive, FALSE);
}

static void
handle_login_as_guest (Greeter *greeter, guint32 sequence_number)
{
    g_debug ("Greeter start authentication for guest account");

    reset_session (greeter);

    if (!greeter->priv->allow_guest)
    {
        g_debug ("Guest account is disabled");
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
        g_debug ("Greeter start authentication for remote session %s", session_name);
        username = NULL;
    }
    else
        g_debug ("Greeter start authentication for remote session %s as user %s", session_name, username);

    reset_session (greeter);

    service = get_remote_session_service (session_name);
    if (!service)
    {
        send_end_authentication (greeter, sequence_number, "", PAM_SYSTEM_ERR);
        return;
    }

    greeter->priv->authentication_sequence_number = sequence_number;
    greeter->priv->remote_session = g_strdup (session_name);
    g_signal_emit (greeter, signals[START_AUTHENTICATION], 0, username, &greeter->priv->authentication_session);
    if (greeter->priv->authentication_session)
    {
        g_signal_connect (G_OBJECT (greeter->priv->authentication_session), "got-messages", G_CALLBACK (pam_messages_cb), greeter);
        g_signal_connect (G_OBJECT (greeter->priv->authentication_session), "authentication-complete", G_CALLBACK (authentication_complete_cb), greeter);

        /* Run the session process */
        session_start (greeter->priv->authentication_session, service, username, TRUE, TRUE, TRUE);
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

    g_debug ("Continue authentication");

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

    g_debug ("Cancel authentication");
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
            g_debug ("Greeter requests session %s", session);
        else
            g_debug ("Greeter requests default session");
        greeter->priv->start_session = TRUE;
        g_signal_emit (greeter, signals[START_SESSION], 0, session_type, session, &result);
    }
    else
    {
        g_debug ("Ignoring start session request, user is not authorized");
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
        g_debug ("Ignoring set language request, user is not authorized");
        return;
    }

    // FIXME: Could use this
    if (greeter->priv->guest_account_authenticated)
    {
        g_debug ("Ignoring set language request for guest user");
        return;
    }

    g_debug ("Greeter sets language %s", language);
    user = session_get_user (greeter->priv->authentication_session);
    user_set_language (user, language);
}

static guint32
read_int (Greeter *greeter, gsize *offset)
{
    guint32 value;
    guint8 *buffer;
    if (greeter->priv->n_read - *offset < sizeof (guint32))
    {
        g_warning ("Not enough space for int, need %zu, got %zu", sizeof (guint32), greeter->priv->n_read - *offset);
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
        g_warning ("Payload length of %u octets too long", payload_length);
        return 0;      
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
        g_warning ("Not enough space for string, need %u, got %zu", length, greeter->priv->n_read - *offset);
        return g_strdup ("");
    }

    value = (*alloc_fn) (sizeof (gchar *) * (length + 1));
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
    int id, i;
    guint32 sequence_number, n_secrets, max_secrets;
    gchar *version, *username, *session_name, *language;
    gchar **secrets;
    GError *error = NULL;

    if (condition == G_IO_HUP)
    {
        g_debug ("Greeter closed communication channel");
        return FALSE;
    }
  
    n_to_read = HEADER_SIZE;
    if (greeter->priv->n_read >= HEADER_SIZE)
    {
        n_to_read = get_message_length (greeter);
        if (n_to_read == 0)
            return FALSE;
    }

    status = g_io_channel_read_chars (greeter->priv->from_greeter_channel,
                                      (gchar *) greeter->priv->read_buffer + greeter->priv->n_read,
                                      n_to_read - greeter->priv->n_read,
                                      &n_read,
                                      &error);
    if (error)
        g_warning ("Error reading from greeter: %s", error->message);
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
        if (n_to_read == 0)
            return FALSE;

        greeter->priv->read_buffer = secure_realloc (greeter, greeter->priv->read_buffer, n_to_read);
        read_cb (source, condition, greeter);
        return TRUE;
    }
  
    offset = 0;
    id = read_int (greeter, &offset);
    read_int (greeter, &offset);
    switch (id)
    {
    case GREETER_MESSAGE_CONNECT:
        version = read_string (greeter, &offset);
        handle_connect (greeter, version);
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
            g_warning ("Array length of %u elements too long", n_secrets);
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
    default:
        g_warning ("Unknown message from greeter: %d", id);
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
greeter_get_start_session (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, FALSE);
    return greeter->priv->start_session;
}

gboolean
greeter_start (Greeter *greeter, const gchar *service, const gchar *username)
{
    int to_greeter_pipe[2], from_greeter_pipe[2];
    gboolean result = FALSE;
    gchar *value;

    /* Create a pipe to talk with the greeter */
    if (pipe (to_greeter_pipe) != 0 || pipe (from_greeter_pipe) != 0)
    {
        g_warning ("Failed to create pipes: %s", strerror (errno));
        return FALSE;
    }
    greeter->priv->to_greeter_channel = g_io_channel_unix_new (to_greeter_pipe[1]);
    g_io_channel_set_encoding (greeter->priv->to_greeter_channel, NULL, NULL);
    greeter->priv->from_greeter_channel = g_io_channel_unix_new (from_greeter_pipe[0]);
    g_io_channel_set_encoding (greeter->priv->from_greeter_channel, NULL, NULL);
    g_io_channel_set_buffered (greeter->priv->from_greeter_channel, FALSE);
    g_io_add_watch (greeter->priv->from_greeter_channel, G_IO_IN | G_IO_HUP, read_cb, greeter);

    /* Let the greeter session know how to communicate with the daemon */
    value = g_strdup_printf ("%d", from_greeter_pipe[1]);
    session_set_env (greeter->priv->session, "LIGHTDM_TO_SERVER_FD", value);
    g_free (value);
    value = g_strdup_printf ("%d", to_greeter_pipe[0]);
    session_set_env (greeter->priv->session, "LIGHTDM_FROM_SERVER_FD", value);
    g_free (value);

    /* Don't allow the daemon end of the pipes to be accessed in child processes */
    fcntl (to_greeter_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl (from_greeter_pipe[0], F_SETFD, FD_CLOEXEC);

    result = session_start (greeter->priv->session, service, username, FALSE, FALSE, FALSE);

    /* Close the session ends of the pipe */
    close (to_greeter_pipe[0]);
    close (from_greeter_pipe[1]);

    return result;
}

static Session *
greeter_real_start_authentication (Greeter *greeter, const gchar *username)
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
    greeter->priv = G_TYPE_INSTANCE_GET_PRIVATE (greeter, GREETER_TYPE, GreeterPrivate);
    greeter->priv->read_buffer = secure_malloc (greeter, HEADER_SIZE);
    greeter->priv->hints = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
greeter_finalize (GObject *object)
{
    Greeter *self;

    self = GREETER (object);

    g_signal_handlers_disconnect_matched (self->priv->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);    
    g_object_unref (self->priv->session);
    g_free (self->priv->pam_service);
    g_free (self->priv->autologin_pam_service);
    secure_free (self, self->priv->read_buffer);
    g_hash_table_unref (self->priv->hints);
    g_free (self->priv->remote_session);
    if (self->priv->authentication_session)
    {
        g_signal_handlers_disconnect_matched (self->priv->authentication_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, self);
        g_object_unref (self->priv->authentication_session);
    }
    if (self->priv->to_greeter_channel)
        g_io_channel_unref (self->priv->to_greeter_channel);
    if (self->priv->from_greeter_channel)
        g_io_channel_unref (self->priv->from_greeter_channel);

    G_OBJECT_CLASS (greeter_parent_class)->finalize (object);
}

static void
greeter_class_init (GreeterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->start_authentication = greeter_real_start_authentication;
    klass->start_session = greeter_real_start_session;
    object_class->finalize = greeter_finalize;

    signals[CONNECTED] =
        g_signal_new ("connected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, connected),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    signals[START_AUTHENTICATION] =
        g_signal_new ("start-authentication",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, start_authentication),
                      g_signal_accumulator_first_wins,
                      NULL,
                      ldm_marshal_OBJECT__STRING,
                      SESSION_TYPE, 1, G_TYPE_STRING);

    signals[START_SESSION] =
        g_signal_new ("start-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, start_session),
                      g_signal_accumulator_true_handled,
                      NULL,
                      ldm_marshal_BOOLEAN__INT_STRING,
                      G_TYPE_BOOLEAN, 2, G_TYPE_INT, G_TYPE_STRING);

    g_type_class_add_private (klass, sizeof (GreeterPrivate));
}
