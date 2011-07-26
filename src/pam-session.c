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

#include "ldm-marshal.h"
#include "pam-session.h"
#include "user.h"

enum {
    AUTHENTICATION_STARTED,
    STARTED,
    GOT_MESSAGES,
    AUTHENTICATION_RESULT,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct PAMSessionPrivate
{
    /* Service to authenticate against */
    gchar *service;

    /* User being authenticated */
    gchar *username;

    /* Authentication thread */
    GThread *authentication_thread;
  
    /* TRUE if the thread is being intentionally stopped */
    gboolean stop_thread;

    /* Messages requested */
    int num_messages;
    const struct pam_message **messages;
    int authentication_result;

    /* Queue to feed responses to the authentication thread */
    GAsyncQueue *authentication_response_queue;

    /* Authentication handle */
    pam_handle_t *pam_handle;
  
    /* TRUE if in an authentication */
    gboolean in_authentication;

    /* TRUE if is authenticated */
    gboolean is_authenticated;

    /* TRUE if in a session */
    gboolean in_session;
};

G_DEFINE_TYPE (PAMSession, pam_session, G_TYPE_OBJECT);

static gchar *passwd_file = NULL;

void
pam_session_set_use_pam (void)
{
    pam_session_set_use_passwd_file (NULL);
}

void
pam_session_set_use_passwd_file (gchar *passwd_file_)
{
    g_free (passwd_file);
    passwd_file = g_strdup (passwd_file_);
}

static int pam_conv_cb (int num_msg, const struct pam_message **msg, struct pam_response **resp, void *app_data);

PAMSession *
pam_session_new (const gchar *service, const gchar *username)
{
    PAMSession *self = g_object_new (PAM_SESSION_TYPE, NULL);
    struct pam_conv conversation = { pam_conv_cb, self };
    int result;

    self->priv->service = g_strdup (service);
    self->priv->username = g_strdup (username);

    if (!passwd_file)
    {
        result = pam_start (self->priv->service, self->priv->username, &conversation, &self->priv->pam_handle);
        g_debug ("pam_start(\"%s\", \"%s\") -> (%p, %d)",
                 self->priv->service,
                 self->priv->username,
                 self->priv->pam_handle,
                 result);
    }

    return self;
}

gboolean
pam_session_get_is_authenticated (PAMSession *session)
{
    g_return_val_if_fail (session != NULL, FALSE);
    return session->priv->is_authenticated;
}

gboolean
pam_session_open (PAMSession *session)
{
    int result = PAM_SUCCESS;

    g_return_val_if_fail (session != NULL, FALSE);

    session->priv->in_session = TRUE;

    if (!passwd_file && getuid () == 0)
    {
        // FIXME: Set X items
        //pam_set_item (session->priv->pam_handle, PAM_TTY, &tty);
        //pam_set_item (session->priv->pam_handle, PAM_XDISPLAY, &display);
        result = pam_open_session (session->priv->pam_handle, 0);
        g_debug ("pam_open_session(%p, 0) -> %d (%s)",
                 session->priv->pam_handle,
                 result,
                 pam_strerror (session->priv->pam_handle, result));

        if (result == PAM_SUCCESS)
        {          
            result = pam_setcred (session->priv->pam_handle, PAM_ESTABLISH_CRED);
            g_debug ("pam_setcred(%p, PAM_ESTABLISH_CRED) -> %d (%s)",
                     session->priv->pam_handle,
                     result,
                     pam_strerror (session->priv->pam_handle, result));
        }
    }

    g_signal_emit (G_OBJECT (session), signals[STARTED], 0);
  
    return result == PAM_SUCCESS;
}

gboolean
pam_session_get_in_session (PAMSession *session)
{
    g_return_val_if_fail (session != NULL, FALSE);
    return session->priv->in_session;
}

static gboolean
notify_messages_cb (gpointer data)
{
    PAMSession *session = data;

    g_signal_emit (G_OBJECT (session), signals[GOT_MESSAGES], 0, session->priv->num_messages, session->priv->messages);

    return FALSE;
}

static int
pam_conv_cb (int num_msg, const struct pam_message **msg,
             struct pam_response **resp, void *app_data)
{
    PAMSession *session = app_data;
    struct pam_response *response;

    /* For some reason after cancelling we still end up here so check for stop as well */
    if (session->priv->stop_thread)
        return PAM_CONV_ERR;  

    /* Notify user */
    session->priv->num_messages = num_msg;
    session->priv->messages = msg;
    g_idle_add (notify_messages_cb, session);

    /* Wait for response */
    response = g_async_queue_pop (session->priv->authentication_response_queue);
    session->priv->num_messages = 0;
    session->priv->messages = NULL;

    /* Cancelled by user */
    if (session->priv->stop_thread)
        return PAM_CONV_ERR;

    *resp = response;

    return PAM_SUCCESS;
}

static void
report_result (PAMSession *session, int result)
{
    session->priv->in_authentication = FALSE;
    session->priv->is_authenticated = result == PAM_SUCCESS;
    g_signal_emit (G_OBJECT (session), signals[AUTHENTICATION_RESULT], 0, result);
}

static gboolean
notify_auth_complete_cb (gpointer data)
{
    PAMSession *session = data;
    int result;

    result = session->priv->authentication_result;
    session->priv->authentication_result = 0;

    g_thread_join (session->priv->authentication_thread);
    session->priv->authentication_thread = NULL;
    g_async_queue_unref (session->priv->authentication_response_queue);
    session->priv->authentication_response_queue = NULL;

    report_result (session, result);

    /* Authentication was cancelled */
    if (session->priv->stop_thread)
        pam_session_cancel (session);

    /* The thread is complete, drop the reference */
    g_object_unref (session);

    return FALSE;
}

static gpointer
authenticate_cb (gpointer data)
{
    PAMSession *session = data;

    session->priv->authentication_result = pam_authenticate (session->priv->pam_handle, 0);
    g_debug ("pam_authenticate(%p, 0) -> %d (%s)",
             session->priv->pam_handle,
             session->priv->authentication_result,
             pam_strerror (session->priv->pam_handle, session->priv->authentication_result));

    if (session->priv->authentication_result == PAM_SUCCESS)
    {
        session->priv->authentication_result = pam_acct_mgmt (session->priv->pam_handle, 0);
        g_debug ("pam_acct_mgmt(%p, 0) -> %d (%s)",
                 session->priv->pam_handle,
                 session->priv->authentication_result,
                 pam_strerror (session->priv->pam_handle, session->priv->authentication_result));

        if (session->priv->authentication_result == PAM_NEW_AUTHTOK_REQD)
        {
            session->priv->authentication_result = pam_chauthtok (session->priv->pam_handle, PAM_CHANGE_EXPIRED_AUTHTOK);
            g_debug ("pam_chauthtok(%p, PAM_CHANGE_EXPIRED_AUTHTOK) -> %d (%s)",
                     session->priv->pam_handle,
                     session->priv->authentication_result,
                     pam_strerror (session->priv->pam_handle, session->priv->authentication_result));
        }
    }

    /* Notify user */
    g_idle_add (notify_auth_complete_cb, session);

    return NULL;
}

static gchar *
get_password (const gchar *username)
{
    gchar *data = NULL, **lines, *password = NULL;
    gint i;
    GError *error = NULL;

    if (!g_file_get_contents (passwd_file, &data, NULL, &error))
        g_warning ("Error loading passwd file: %s", error->message);
    g_clear_error (&error);

    if (!data)
        return NULL;

    lines = g_strsplit (data, "\n", -1);
    g_free (data);

    for (i = 0; lines[i] && password == NULL; i++)
    {
        gchar *line, **fields;

        line = g_strstrip (lines[i]);
        fields = g_strsplit (line, ":", -1);
        if (g_strv_length (fields) == 7 && strcmp (fields[0], username) == 0)
            password = g_strdup (fields[1]);
        g_strfreev (fields);
    }
    g_strfreev (lines);

    return password;
}

static void
send_message (PAMSession *session, gint style, const gchar *text)
{
    struct pam_message **messages;

    messages = calloc (1, sizeof (struct pam_message *));
    messages[0] = g_malloc0 (sizeof (struct pam_message));
    messages[0]->msg_style = style;
    messages[0]->msg = g_strdup (text);
    session->priv->messages = (const struct pam_message **) messages;
    session->priv->num_messages = 1;

    g_signal_emit (G_OBJECT (session), signals[GOT_MESSAGES], 0, session->priv->num_messages, session->priv->messages);
}

gboolean
pam_session_authenticate (PAMSession *session, GError **error)
{
    g_return_val_if_fail (session != NULL, FALSE);
    g_return_val_if_fail (session->priv->in_authentication == FALSE, FALSE);
    g_return_val_if_fail (session->priv->is_authenticated == FALSE, FALSE);

    session->priv->in_authentication = TRUE;
    g_signal_emit (G_OBJECT (session), signals[AUTHENTICATION_STARTED], 0);

    if (passwd_file)
    {
        if (session->priv->username == NULL)
            send_message (session, PAM_PROMPT_ECHO_ON, "login:");
        else
        {
            gchar *password;

            password = get_password (session->priv->username);
            /* Always succeed with autologin, or no password on account otherwise prompt for a password */
            if (strcmp (session->priv->service, "lightdm-autologin") == 0 || g_strcmp0 (password, "") == 0)
                report_result (session, PAM_SUCCESS);
            else
                send_message (session, PAM_PROMPT_ECHO_OFF, "Password:");
            g_free (password);
        }      
    }
    else
    {
        /* Hold a reference to this object while the thread may access it */
        g_object_ref (session);

        /* Start thread */
        session->priv->authentication_response_queue = g_async_queue_new ();
        session->priv->authentication_thread = g_thread_create (authenticate_cb, session, TRUE, error);
        if (!session->priv->authentication_thread)
            return FALSE;
    }

    return TRUE;
}

const gchar *
pam_session_strerror (PAMSession *session, int error)
{
    g_return_val_if_fail (session != NULL, NULL);
    return pam_strerror (session->priv->pam_handle, error);
}

const gchar *
pam_session_get_username (PAMSession *session)
{
    const char *username;

    g_return_val_if_fail (session != NULL, NULL);

    if (!passwd_file && session->priv->pam_handle)
    {
        g_free (session->priv->username);
        pam_get_item (session->priv->pam_handle, PAM_USER, (const void **) &username);
        session->priv->username = g_strdup (username);
    }

    return session->priv->username;
}

const struct pam_message **
pam_session_get_messages (PAMSession *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    return session->priv->messages;  
}

gint
pam_session_get_num_messages (PAMSession *session)
{
    g_return_val_if_fail (session != NULL, 0);
    return session->priv->num_messages;
}

void
pam_session_respond (PAMSession *session, struct pam_response *response)
{
    g_return_if_fail (session != NULL);

    if (passwd_file)
    {
        gchar *password;

        if (session->priv->messages)
        {
            int i;
            struct pam_message **messages = (struct pam_message **) session->priv->messages;

            for (i = 0; i < session->priv->num_messages; i++)
            {
                g_free ((gchar *) messages[i]->msg);
                g_free (messages[i]);
            }
            g_free (messages);
            session->priv->messages = NULL;
            session->priv->num_messages = 0;
        }

        if (session->priv->username == NULL)
        {
            session->priv->username = g_strdup (response->resp);
            password = get_password (session->priv->username);
            if (g_strcmp0 (password, "") == 0)
                report_result (session, PAM_SUCCESS);
            else
                send_message (session, PAM_PROMPT_ECHO_OFF, "Password:");
        }
        else
        {
            User *user;

            user = user_get_by_name (session->priv->username);
            password = get_password (session->priv->username);
            if (user && g_strcmp0 (response->resp, password) == 0)
                report_result (session, PAM_SUCCESS);
            else
                report_result (session, PAM_AUTH_ERR);

            if (user)
                g_object_unref (user);
        }
        g_free (password);
        g_free (response->resp);
        g_free (response);
    }
    else
    {
        g_return_if_fail (session->priv->authentication_thread != NULL);
        g_async_queue_push (session->priv->authentication_response_queue, response);
    }
}

void
pam_session_cancel (PAMSession *session)
{
    g_return_if_fail (session != NULL);

    /* If authenticating cancel first */
    if (passwd_file)
    {
        if (session->priv->in_authentication)
            report_result (session, PAM_CONV_ERR);

    }
    else if (session->priv->authentication_thread)
    {
        session->priv->stop_thread = TRUE;
        g_async_queue_push (session->priv->authentication_response_queue, GINT_TO_POINTER (-1));
    }
}

const gchar *
pam_session_getenv (PAMSession *session, const gchar *name)
{
    g_return_val_if_fail (session != NULL, NULL);
    if (passwd_file)
        return NULL;
    else
        return pam_getenv (session->priv->pam_handle, name);
}

gchar **
pam_session_get_envlist (PAMSession *session)
{
    g_return_val_if_fail (session != NULL, NULL);
    if (passwd_file)
    {
        char **env_list = calloc (1, sizeof (gchar *));
        env_list[0] = NULL;
        return env_list;
    }
    else
        return pam_getenvlist (session->priv->pam_handle);
}

void
pam_session_close (PAMSession *session)
{
    int result;

    g_return_if_fail (session != NULL);

    session->priv->in_session = FALSE;

    if (!passwd_file && getuid () == 0)
    {
        g_return_if_fail (session->priv->pam_handle != NULL);

        result = pam_close_session (session->priv->pam_handle, 0);
        g_debug ("pam_close_session(%p) -> %d (%s)",
                 session->priv->pam_handle,
                 result,
                 pam_strerror (session->priv->pam_handle, result));

        result = pam_setcred (session->priv->pam_handle, PAM_DELETE_CRED);
        g_debug ("pam_setcred(%p, PAM_DELETE_CRED) -> %d (%s)",
                 session->priv->pam_handle,
                 result,
                 pam_strerror (session->priv->pam_handle, result));

        result = pam_end (session->priv->pam_handle, PAM_SUCCESS);
        g_debug ("pam_end(%p) -> %d",
                 session->priv->pam_handle,
                 result);

        session->priv->pam_handle = NULL;
    }
}

static void
pam_session_init (PAMSession *session)
{
    session->priv = G_TYPE_INSTANCE_GET_PRIVATE (session, PAM_SESSION_TYPE, PAMSessionPrivate);
}

static void
pam_session_finalize (GObject *object)
{
    PAMSession *self;

    self = PAM_SESSION (object);

    g_free (self->priv->service);  
    g_free (self->priv->username);
    if (self->priv->pam_handle)
        pam_end (self->priv->pam_handle, PAM_SUCCESS);

    G_OBJECT_CLASS (pam_session_parent_class)->finalize (object);
}

static void
pam_session_class_init (PAMSessionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = pam_session_finalize;  

    g_type_class_add_private (klass, sizeof (PAMSessionPrivate));

    signals[AUTHENTICATION_STARTED] =
        g_signal_new ("authentication-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PAMSessionClass, authentication_started),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    signals[STARTED] =
        g_signal_new ("started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PAMSessionClass, started),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    signals[GOT_MESSAGES] =
        g_signal_new ("got-messages",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PAMSessionClass, got_messages),
                      NULL, NULL,
                      ldm_marshal_VOID__INT_POINTER,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);

    signals[AUTHENTICATION_RESULT] =
        g_signal_new ("authentication-result",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PAMSessionClass, authentication_result),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__INT,
                      G_TYPE_NONE, 1, G_TYPE_INT);
}
