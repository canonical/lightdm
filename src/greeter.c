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

#include <string.h>

#include "greeter.h"
#include "ldm-marshal.h"
#include "greeter-protocol.h"

/* Length of time in milliseconds to wait for a greeter to quit */
#define GREETER_QUIT_TIMEOUT 1000

enum {
    LOGIN,
    QUIT,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct GreeterPrivate
{
    gboolean connected;

    /* Pipe to communicate to greeter */
    int pipe[2];
    gchar *read_buffer;
    gsize n_read;
  
    gchar *theme;
    gchar *layout;
    gchar *session;
    gchar *default_user;
    gint autologin_timeout;

    guint quit_timeout;
  
    PAMSession *pam_session;    
};

G_DEFINE_TYPE (Greeter, greeter, SESSION_TYPE);

Greeter *
greeter_new (void)
{
    return g_object_new (GREETER_TYPE, NULL);
}

void
greeter_set_default_user (Greeter *greeter, const gchar *username, gint timeout)
{
    g_free (greeter->priv->default_user);
    greeter->priv->default_user = g_strdup (username);
    greeter->priv->autologin_timeout = timeout;
}

void
greeter_set_theme (Greeter *greeter, const gchar *theme)
{
    g_free (greeter->priv->theme);
    greeter->priv->theme = g_strdup (theme);
}

void
greeter_set_layout (Greeter *greeter, const gchar *layout)
{
    g_free (greeter->priv->layout);
    greeter->priv->layout = g_strdup (layout);
}

const gchar *
greeter_get_layout (Greeter *greeter)
{
    return greeter->priv->layout;
}

void
greeter_set_session (Greeter *greeter, const gchar *session)
{
    g_free (greeter->priv->session);
    greeter->priv->session = g_strdup (session);
}

const gchar *
greeter_get_session (Greeter *greeter)
{
    return greeter->priv->session;
}

PAMSession *
greeter_get_pam_session (Greeter *greeter)
{
    return greeter->priv->pam_session;
}

static guint32
int_length ()
{
    return 4;
}

static void
write_int (Greeter *greeter, guint32 value)
{
    gchar buffer[4];
    buffer[0] = value >> 24;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;
    g_io_channel_write_chars (child_process_get_to_child_channel (CHILD_PROCESS (greeter)), buffer, int_length (), NULL, NULL);
}

static void
write_string (Greeter *greeter, const gchar *value)
{
    write_int (greeter, strlen (value));
    g_io_channel_write_chars (child_process_get_to_child_channel (CHILD_PROCESS (greeter)), value, -1, NULL, NULL);  
}

static void
write_header (Greeter *greeter, guint32 id, guint32 length)
{
    write_int (greeter, id);
    write_int (greeter, length);
}

static guint32
string_length (const gchar *value)
{
    return int_length () + strlen (value);
}

static void
flush (Greeter *greeter)
{
    g_io_channel_flush (child_process_get_to_child_channel (CHILD_PROCESS (greeter)), NULL);
}

static void
handle_connect (Greeter *greeter)
{
    gchar *theme;

    if (!greeter->priv->connected)
    {
        greeter->priv->connected = TRUE;
        g_debug ("Greeter connected");
    }

    theme = g_build_filename (THEME_DIR, greeter->priv->theme, "index.theme", NULL);

    write_header (greeter, GREETER_MESSAGE_CONNECTED, string_length (theme) + string_length (greeter->priv->layout) + string_length (greeter->priv->session) + string_length (greeter->priv->default_user ? greeter->priv->default_user : "") + int_length ());
    write_string (greeter, theme);
    write_string (greeter, greeter->priv->layout);
    write_string (greeter, greeter->priv->session);
    write_string (greeter, greeter->priv->default_user ? greeter->priv->default_user : "");
    write_int (greeter, greeter->priv->autologin_timeout);
    flush (greeter);

    g_free (theme);
}

static void
pam_messages_cb (PAMSession *session, int num_msg, const struct pam_message **msg, Greeter *greeter)
{
    int i;
    guint32 size;

    /* Respond to d-bus query with messages */
    g_debug ("Prompt greeter with %d message(s)", num_msg);
    size = int_length ();
    for (i = 0; i < num_msg; i++)
        size += int_length () + string_length (msg[i]->msg);
    write_header (greeter, GREETER_MESSAGE_PROMPT_AUTHENTICATION, size);
    write_int (greeter, num_msg);
    for (i = 0; i < num_msg; i++)
    {
        write_int (greeter, msg[i]->msg_style);
        write_string (greeter, msg[i]->msg);
    }
    flush (greeter);  
}

static void
authenticate_result_cb (PAMSession *session, int result, Greeter *greeter)
{
    g_debug ("Authenticate result for user %s: %s", pam_session_get_username (greeter->priv->pam_session), pam_session_strerror (greeter->priv->pam_session, result));

    if (result == PAM_SUCCESS)
    {
        //run_script ("PostLogin");
        pam_session_authorize (session);
    }

    /* Respond to D-Bus request */
    write_header (greeter, GREETER_MESSAGE_END_AUTHENTICATION, int_length ());
    write_int (greeter, result);   
    flush (greeter);
}

static void
handle_start_authentication (Greeter *greeter, const gchar *username)
{
    GError *error = NULL;

    // FIXME
    //if (greeter->priv->user_session)
    //    return;

    /* Abort existing authentication */
    if (greeter->priv->pam_session)
    {
        g_signal_handlers_disconnect_matched (greeter->priv->pam_session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, greeter);
        pam_session_end (greeter->priv->pam_session);
        g_object_unref (greeter->priv->pam_session);
    }

    g_debug ("Greeter start authentication for %s", username);

    greeter->priv->pam_session = pam_session_new ("lightdm"/*FIXMEgreeter->priv->pam_service*/, username);
    g_signal_connect (G_OBJECT (greeter->priv->pam_session), "got-messages", G_CALLBACK (pam_messages_cb), greeter);
    g_signal_connect (G_OBJECT (greeter->priv->pam_session), "authentication-result", G_CALLBACK (authenticate_result_cb), greeter);

    if (!pam_session_start (greeter->priv->pam_session, &error))
        g_warning ("Failed to start authentication: %s", error->message);
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
    write_header (greeter, GREETER_MESSAGE_QUIT, 0);
    flush (greeter);

    if (greeter->priv->quit_timeout)
        g_source_remove (greeter->priv->quit_timeout);
    greeter->priv->quit_timeout = g_timeout_add (GREETER_QUIT_TIMEOUT, quit_greeter_cb, greeter);
}

static void
handle_login (Greeter *greeter, gchar *username, gchar *session, gchar *language)
{
    /*if (greeter->priv->user_session != NULL)
    {
        g_warning ("Ignoring request to log in when already logged in");
        return;
    }*/

    g_debug ("Greeter login for user %s on session %s", username, session);

    g_signal_emit (greeter, signals[LOGIN], 0, username, session, language);
}

#define HEADER_SIZE (sizeof (guint32) * 2)

static guint32
read_int (Greeter *greeter, gsize *offset)
{
    guint32 value;
    gchar *buffer;
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
    gchar *username, *session_name, *language;
    gchar **secrets;
    GError *error = NULL;
  
    n_to_read = HEADER_SIZE;
    if (greeter->priv->n_read >= HEADER_SIZE)
    {
        offset = int_length ();
        n_to_read += read_int (greeter, &offset);
    }

    status = g_io_channel_read_chars (child_process_get_from_child_channel (CHILD_PROCESS (greeter)),
                                      greeter->priv->read_buffer + greeter->priv->n_read,
                                      n_to_read - greeter->priv->n_read,
                                      &n_read,
                                      &error);
    if (status != G_IO_STATUS_NORMAL)
        g_warning ("Error reading from greeter: %s", error->message);
    g_clear_error (&error);
    if (status != G_IO_STATUS_NORMAL)
        return;

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
    case GREETER_MESSAGE_START_AUTHENTICATION:
        username = read_string (greeter, &offset);
        handle_start_authentication (greeter, username);
        g_free (username);
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
    case GREETER_MESSAGE_LOGIN:
        username = read_string (greeter, &offset);
        session_name = read_string (greeter, &offset);
        language = read_string (greeter, &offset);
        handle_login (greeter, username, session_name, language);
        g_free (username);
        g_free (session_name);
        g_free (language);
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

    g_signal_emit (greeter, signals[QUIT], 0);
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
    g_free (self->priv->layout);
    g_free (self->priv->session);
    g_free (self->priv->default_user);
}

static void
greeter_class_init (GreeterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = greeter_finalize;

    signals[LOGIN] =
        g_signal_new ("login",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, login),
                      NULL, NULL,
                      ldm_marshal_VOID__STRING_STRING_STRING,
                      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    signals[QUIT] =
        g_signal_new ("quit",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (GreeterClass, quit),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    g_type_class_add_private (klass, sizeof (GreeterPrivate));
}
