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

#include <stdlib.h>
#include <string.h>

#include "greeter.h"
#include "configuration.h"
#include "dmrc.h"
#include "ldm-marshal.h"
#include "greeter-protocol.h"
#include "guest-account.h"

/* Length of time in milliseconds to wait for a greeter to quit */
#define GREETER_QUIT_TIMEOUT 1000

enum {
    START_SESSION,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct GreeterPrivate
{
    /* The number of greeters before this one */
    guint count;

    /* TRUE if the greeter has connected to the daemon pipe */
    gboolean connected;

    /* Pipe to communicate to greeter */
    int pipe[2];

    /* Buffer for data read from greeter */
    guint8 *read_buffer;
    gsize n_read;

    /* Theme for greeter to use */
    gchar *theme;

    /* Default session to use */   
    gchar *default_session;

    /* Default user to log in as, or NULL for no default */
    gchar *default_user;

    /* Time in seconds to wait until logging in as default user */
    gint autologin_timeout;

    /* Timeout for greeter to respond to quit request */
    guint quit_timeout;

    /* PAM session being constructed by the greeter */
    PAMSession *pam_session;

    /* TRUE if logging into guest session */
    gboolean using_guest_account;
};

G_DEFINE_TYPE (Greeter, greeter, SESSION_TYPE);

Greeter *
greeter_new (const gchar *theme, guint count)
{
    Greeter *greeter = g_object_new (GREETER_TYPE, NULL);

    greeter->priv->theme = g_strdup (theme);
    greeter->priv->count = count;

    return greeter;
}

void
greeter_set_default_user (Greeter *greeter, const gchar *username, gint timeout)
{
    g_return_if_fail (greeter != NULL);

    g_free (greeter->priv->default_user);
    greeter->priv->default_user = g_strdup (username);
    greeter->priv->autologin_timeout = timeout;
}

const gchar *
greeter_get_theme (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->theme;
}

void
greeter_set_default_session (Greeter *greeter, const gchar *session)
{
    g_return_if_fail (greeter != NULL);

    g_free (greeter->priv->default_session);
    greeter->priv->default_session = g_strdup (session);
}

const gchar *
greeter_get_default_session (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->default_session;
}

PAMSession *
greeter_get_pam_session (Greeter *greeter)
{
    g_return_val_if_fail (greeter != NULL, NULL);
    return greeter->priv->pam_session;
}

static guint32
int_length ()
{
    return 4;
}

#define HEADER_SIZE (sizeof (guint32) * 2)
#define MAX_MESSAGE_LENGTH 1024

static void
write_message (Greeter *greeter, guint8 *message, gint message_length)
{
    GError *error = NULL;
    if (g_io_channel_write_chars (child_process_get_to_child_channel (CHILD_PROCESS (greeter)), (gchar *) message, message_length, NULL, &error) != G_IO_STATUS_NORMAL)
        g_warning ("Error writing to greeter: %s", error->message);
    g_clear_error (&error);
    g_io_channel_flush (child_process_get_to_child_channel (CHILD_PROCESS (greeter)), NULL);
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

static void
write_header (guint8 *buffer, gint buffer_length, guint32 id, guint32 length, gsize *offset)
{
    write_int (buffer, buffer_length, id, offset);
    write_int (buffer, buffer_length, length, offset);
}

static guint32
string_length (const gchar *value)
{
    return int_length () + strlen (value);
}

static void
handle_connect (Greeter *greeter)
{
    gchar *theme_dir, *theme;
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    if (!greeter->priv->connected)
    {
        greeter->priv->connected = TRUE;
        g_debug ("Greeter connected");
    }

    theme_dir = config_get_string (config_get_instance (), "LightDM", "theme-directory");  
    theme = g_build_filename (theme_dir, greeter->priv->theme, "index.theme", NULL);
    g_free (theme_dir);

    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_CONNECTED, string_length (theme) + string_length (greeter->priv->default_session) + string_length (greeter->priv->default_user ? greeter->priv->default_user : "") + int_length () + int_length () + int_length (), &offset);
    write_string (message, MAX_MESSAGE_LENGTH, theme, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, greeter->priv->default_session, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, greeter->priv->default_user ? greeter->priv->default_user : "", &offset);
    write_int (message, MAX_MESSAGE_LENGTH, greeter->priv->autologin_timeout, &offset);
    write_int (message, MAX_MESSAGE_LENGTH, guest_account_get_is_enabled (), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, greeter->priv->count, &offset);
    write_message (greeter, message, offset);

    g_free (theme);
}

static void
pam_messages_cb (PAMSession *session, int num_msg, const struct pam_message **msg, Greeter *greeter)
{
    int i;
    guint32 size;
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    /* Respond to d-bus query with messages */
    g_debug ("Prompt greeter with %d message(s)", num_msg);
    size = int_length ();
    for (i = 0; i < num_msg; i++)
        size += int_length () + string_length (msg[i]->msg);
    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_PROMPT_AUTHENTICATION, size, &offset);
    write_int (message, MAX_MESSAGE_LENGTH, num_msg, &offset);
    for (i = 0; i < num_msg; i++)
    {
        write_int (message, MAX_MESSAGE_LENGTH, msg[i]->msg_style, &offset);
        write_string (message, MAX_MESSAGE_LENGTH, msg[i]->msg, &offset);
    }
    write_message (greeter, message, offset);  
}

static void
send_end_authentication (Greeter *greeter, int result)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_END_AUTHENTICATION, int_length (), &offset);
    write_int (message, MAX_MESSAGE_LENGTH, result, &offset);
    write_message (greeter, message, offset); 
}

static void
authentication_result_cb (PAMSession *session, int result, Greeter *greeter)
{
    g_debug ("Authenticate result for user %s: %s", pam_session_get_username (greeter->priv->pam_session), pam_session_strerror (greeter->priv->pam_session, result));

    if (result == PAM_SUCCESS)
    {
        g_debug ("User %s authorized", pam_session_get_username (session));
        //run_script ("PostLogin");
        pam_session_authorize (session);
    }

    send_end_authentication (greeter, result);
}

static void
reset_session (Greeter *greeter)
{
    if (greeter->priv->pam_session == NULL)
        return;

    g_signal_handlers_disconnect_matched (greeter->priv->pam_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, greeter);
    pam_session_end (greeter->priv->pam_session);
    g_object_unref (greeter->priv->pam_session);

    if (greeter->priv->using_guest_account)
        guest_account_unref ();
    greeter->priv->using_guest_account = FALSE;
}

static void
start_authentication (Greeter *greeter, PAMSession *session)
{
    GError *error = NULL;

    // FIXME
    //if (greeter->priv->user_session)
    //    return;

    greeter->priv->pam_session = session;

    g_signal_connect (G_OBJECT (greeter->priv->pam_session), "got-messages", G_CALLBACK (pam_messages_cb), greeter);
    g_signal_connect (G_OBJECT (greeter->priv->pam_session), "authentication-result", G_CALLBACK (authentication_result_cb), greeter);

    if (!pam_session_start (greeter->priv->pam_session, &error))
    {
        g_warning ("Failed to start authentication: %s", error->message);
        send_end_authentication (greeter, PAM_SYSTEM_ERR);
    }
}

static void
handle_login (Greeter *greeter, const gchar *username)
{
    if (username[0] == '\0')
    {
        g_debug ("Greeter start authentication");
        username = NULL;
    }
    else
        g_debug ("Greeter start authentication for %s", username);        

    reset_session (greeter);
    greeter->priv->using_guest_account = FALSE;
    start_authentication (greeter, pam_session_new ("lightdm"/*FIXMEgreeter->priv->pam_service*/, username));
}

static void
handle_login_as_guest (Greeter *greeter)
{
    g_debug ("Greeter start authentication for guest account");

    reset_session (greeter);

    if (!guest_account_get_is_enabled ())
    {
        g_debug ("Guest account is disabled");
        send_end_authentication (greeter, PAM_USER_UNKNOWN);
        return;
    }

    if (!guest_account_ref ())
    {
        g_debug ("Unable to create guest account");
        send_end_authentication (greeter, PAM_USER_UNKNOWN);
        return;
    }
    greeter->priv->using_guest_account = TRUE;

    start_authentication (greeter, pam_session_new ("lightdm-autologin"/*FIXMEgreeter->priv->pam_service*/, guest_account_get_username ()));
}

static void
handle_continue_authentication (Greeter *greeter, gchar **secrets)
{
    int num_messages;
    const struct pam_message **messages;
    struct pam_response *response;
    int i, j, n_secrets = 0;

    /* Not connected */
    if (!greeter->priv->connected)
        return;

    /* Not in authorization */
    if (greeter->priv->pam_session == NULL)
        return;

    num_messages = pam_session_get_num_messages (greeter->priv->pam_session);
    messages = pam_session_get_messages (greeter->priv->pam_session);

    /* Check correct number of responses */
    for (i = 0; i < num_messages; i++)
    {
        int msg_style = messages[i]->msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
            n_secrets++;
    }
    if (g_strv_length (secrets) != n_secrets)
    {
        pam_session_end (greeter->priv->pam_session);
        return;
    }

    g_debug ("Continue authentication");

    /* Build response */
    response = calloc (num_messages, sizeof (struct pam_response));  
    for (i = 0, j = 0; i < num_messages; i++)
    {
        int msg_style = messages[i]->msg_style;
        if (msg_style == PAM_PROMPT_ECHO_OFF || msg_style == PAM_PROMPT_ECHO_ON)
        {
            response[i].resp = strdup (secrets[j]); // FIXME: Need to convert from UTF-8
            j++;
        }
    }

    pam_session_respond (greeter->priv->pam_session, response);
}

static void
handle_cancel_authentication (Greeter *greeter)
{
    /* Not connected */
    if (!greeter->priv->connected)
        return;

    /* Not in authorization */
    if (greeter->priv->pam_session == NULL)
        return;

    g_debug ("Cancel authentication");

    pam_session_cancel (greeter->priv->pam_session);
}

static gboolean
quit_greeter_cb (gpointer data)
{
    Greeter *greeter = data;
    g_warning ("Greeter did not quit, sending kill signal");
    session_stop (SESSION (greeter));
    greeter->priv->quit_timeout = 0;
    return TRUE;
}

void
greeter_quit (Greeter *greeter)
{
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    g_return_if_fail (greeter != NULL);

    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_QUIT, 0, &offset);
    write_message (greeter, message, offset);

    if (greeter->priv->quit_timeout)
        g_source_remove (greeter->priv->quit_timeout);
    greeter->priv->quit_timeout = g_timeout_add (GREETER_QUIT_TIMEOUT, quit_greeter_cb, greeter);
}

static void
handle_start_session (Greeter *greeter, gchar *session)
{
    /*if (greeter->priv->user_session != NULL)
    {
        g_warning ("Ignoring request to log in when already logged in");
        return;
    }*/

    g_debug ("Greeter start session %s", session);

    g_signal_emit (greeter, signals[START_SESSION], 0, session);
}

static void
handle_get_user_defaults (Greeter *greeter, gchar *username)
{
    GKeyFile *dmrc_file;
    gchar *language, *layout, *session;
    guint8 message[MAX_MESSAGE_LENGTH];
    gsize offset = 0;

    /* Load the users login settings (~/.dmrc) */
    dmrc_file = dmrc_load (username);

    language = g_key_file_get_string (dmrc_file, "Desktop", "Language", NULL);
    if (!language)
        language = g_strdup ("");
    layout = g_key_file_get_string (dmrc_file, "Desktop", "Layout", NULL);
    if (!layout)
        layout = g_strdup ("");
    session = g_key_file_get_string (dmrc_file, "Desktop", "Session", NULL);
    if (!session)
        session = g_strdup ("");

    write_header (message, MAX_MESSAGE_LENGTH, GREETER_MESSAGE_USER_DEFAULTS, string_length (language) + string_length (layout) + string_length (session), &offset);
    write_string (message, MAX_MESSAGE_LENGTH, language, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, layout, &offset);
    write_string (message, MAX_MESSAGE_LENGTH, session, &offset);
    write_message (greeter, message, offset);
  
    g_free (language);
    g_free (layout);
    g_free (session);

    g_key_file_free (dmrc_file);
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

static gchar *
read_string (Greeter *greeter, gsize *offset)
{
    guint32 length;
    gchar *value;

    length = read_int (greeter, offset);
    if (greeter->priv->n_read - *offset < length)
    {
        g_warning ("Not enough space for string, need %u, got %zu", length, greeter->priv->n_read - *offset);
        return g_strdup ("");
    }

    value = g_malloc (sizeof (gchar *) * (length + 1));
    memcpy (value, greeter->priv->read_buffer + *offset, length);
    value[length] = '\0';
    *offset += length;

    return value;
}

static void
got_data_cb (Greeter *greeter)
{
    gsize n_to_read, n_read, offset;
    GIOStatus status;
    int id, n_secrets, i;
    gchar *username, *session_name;
    gchar **secrets;
    GError *error = NULL;
  
    n_to_read = HEADER_SIZE;
    if (greeter->priv->n_read >= HEADER_SIZE)
    {
        offset = int_length ();
        n_to_read += read_int (greeter, &offset);
    }

    status = g_io_channel_read_chars (child_process_get_from_child_channel (CHILD_PROCESS (greeter)),
                                      (gchar *) greeter->priv->read_buffer + greeter->priv->n_read,
                                      n_to_read - greeter->priv->n_read,
                                      &n_read,
                                      &error);
    if (status != G_IO_STATUS_NORMAL)
        g_warning ("Error reading from greeter: %s", error->message);
    g_clear_error (&error);
    if (status != G_IO_STATUS_NORMAL)
        return;

    g_debug ("Read %zi bytes from greeter", n_read);
    /*for (i = 0; i < n_read; i++)
       g_print ("%02X ", greeter->priv->read_buffer[greeter->priv->n_read+i]);
    g_print ("\n");*/

    greeter->priv->n_read += n_read;
    if (greeter->priv->n_read != n_to_read)
        return;

    /* If have header, rerun for content */
    if (greeter->priv->n_read == HEADER_SIZE)
    {
        n_to_read = ((guint32 *) greeter->priv->read_buffer)[1];
        if (n_to_read > 0)
        {
            greeter->priv->read_buffer = g_realloc (greeter->priv->read_buffer, HEADER_SIZE + n_to_read);
            got_data_cb (greeter);
            return;
        }
    }
  
    offset = 0;
    id = read_int (greeter, &offset);
    read_int (greeter, &offset);
    switch (id)
    {
    case GREETER_MESSAGE_CONNECT:
        handle_connect (greeter);
        break;
    case GREETER_MESSAGE_LOGIN:
        username = read_string (greeter, &offset);
        handle_login (greeter, username);
        g_free (username);
        break;
    case GREETER_MESSAGE_LOGIN_AS_GUEST:
        handle_login_as_guest (greeter);
        break;
    case GREETER_MESSAGE_CONTINUE_AUTHENTICATION:
        n_secrets = read_int (greeter, &offset);
        secrets = g_malloc (sizeof (gchar *) * (n_secrets + 1));
        for (i = 0; i < n_secrets; i++)
            secrets[i] = read_string (greeter, &offset);
        secrets[i] = NULL;
        handle_continue_authentication (greeter, secrets);
        g_strfreev (secrets);
        break;
    case GREETER_MESSAGE_CANCEL_AUTHENTICATION:
        handle_cancel_authentication (greeter);
        break;
    case GREETER_MESSAGE_START_SESSION:
        session_name = read_string (greeter, &offset);
        handle_start_session (greeter, session_name);
        g_free (session_name);
        break;
    case GREETER_MESSAGE_GET_USER_DEFAULTS:
        username = read_string (greeter, &offset);
        handle_get_user_defaults (greeter, username);
        g_free (username);
        break;
    default:
        g_warning ("Unknown message from greeter: %d", id);
        break;
    }

    greeter->priv->n_read = 0;
}

static void
end_session (Greeter *greeter, gboolean clean_exit)
{  
    if (greeter->priv->quit_timeout)
    {
        g_source_remove (greeter->priv->quit_timeout);
        greeter->priv->quit_timeout = 0;
    }

    /*if (!clean_exit)
        g_warning ("Greeter failed");
    else if (!greeter_connected)
        g_warning ("Greeter quit before connecting");
    else if (!display->priv->user_session)
        g_warning ("Greeter quit before session started");
    else
        return;*/

    // FIXME: Issue with greeter, don't want to start a new one, report error to user
}

static void
session_exited_cb (Greeter *greeter, gint status)
{
    end_session (greeter, status == 0);
}

static void
session_terminated_cb (Greeter *greeter, gint signum)
{
    end_session (greeter, FALSE);
}

static void
greeter_init (Greeter *greeter)
{
    greeter->priv = G_TYPE_INSTANCE_GET_PRIVATE (greeter, GREETER_TYPE, GreeterPrivate);
    greeter->priv->read_buffer = g_malloc (HEADER_SIZE);
    g_signal_connect (G_OBJECT (greeter), "got-data", G_CALLBACK (got_data_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "exited", G_CALLBACK (session_exited_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "terminated", G_CALLBACK (session_terminated_cb), NULL);
}

static void
greeter_finalize (GObject *object)
{
    Greeter *self;

    self = GREETER (object);

    g_free (self->priv->read_buffer);
    g_free (self->priv->theme);
    g_free (self->priv->default_session);
    g_free (self->priv->default_user);
    if (self->priv->using_guest_account)
        guest_account_unref ();

    G_OBJECT_CLASS (greeter_parent_class)->finalize (object);
}

static void
greeter_class_init (GreeterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = greeter_finalize;

    signals[START_SESSION] =
        g_signal_new ("start-session",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, start_session),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    g_type_class_add_private (klass, sizeof (GreeterPrivate));
}
