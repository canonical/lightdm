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

#include "ldm-marshal.h"
#include "pam-session.h"

enum {
    AUTHENTICATION_STARTED,
    STARTED,
    GOT_MESSAGES,
    AUTHENTICATION_RESULT,
    ENDED,
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
    gboolean stop;

    /* TRUE if the conversation should be cancelled */
    gboolean cancel;

    /* Messages requested */
    int num_messages;
    const struct pam_message **messages;
    int result;

    /* Queue to feed responses to the authentication thread */
    GAsyncQueue *authentication_response_queue;

    /* Authentication handle */
    pam_handle_t *pam_handle;
  
    /* TRUE if in a session */
    gboolean in_session;
};

G_DEFINE_TYPE (PAMSession, pam_session, G_TYPE_OBJECT);

PAMSession *
pam_session_new (const gchar *service, const gchar *username)
{
    PAMSession *self = g_object_new (PAM_SESSION_TYPE, NULL);

    self->priv->service = g_strdup (service);
    self->priv->username = g_strdup (username);

    return self;
}

gboolean
pam_session_get_in_session (PAMSession *session)
{
    return session->priv->in_session;
}

void
pam_session_authorize (PAMSession *session)
{
    session->priv->in_session = TRUE;

    // FIXME:
    //pam_set_item (session->priv->pam_handle, PAM_TTY, &tty);
    //pam_set_item (session->priv->pam_handle, PAM_XDISPLAY, &display);

    pam_open_session (session->priv->pam_handle, 0);
    g_signal_emit (G_OBJECT (session), signals[STARTED], 0);
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

    /* Notify user */
    session->priv->num_messages = num_msg;
    session->priv->messages = msg;
    g_idle_add (notify_messages_cb, session);

    /* Wait for response */
    response = g_async_queue_pop (session->priv->authentication_response_queue);
    session->priv->num_messages = 0;
    session->priv->messages = NULL;

    /* Cancelled by user */
    if (session->priv->stop || session->priv->cancel)
    {
        session->priv->cancel = FALSE;
        return PAM_CONV_ERR;
    }

    *resp = response;

    return PAM_SUCCESS;
}

static gboolean
notify_auth_complete_cb (gpointer data)
{
    PAMSession *session = data;
    int result;

    result = session->priv->result;
    session->priv->result = 0;

    g_thread_join (session->priv->authentication_thread);
    session->priv->authentication_thread = NULL;
    g_async_queue_unref (session->priv->authentication_response_queue);
    session->priv->authentication_response_queue = NULL;

    /* Authentication was cancelled */
    if (session->priv->stop)
        pam_session_end (session);
    else
        g_signal_emit (G_OBJECT (session), signals[AUTHENTICATION_RESULT], 0, result);

    /* The thread is complete, drop the reference */
    g_object_unref (session);

    return FALSE;
}

static gpointer
authenticate_cb (gpointer data)
{
    PAMSession *session = data;
    struct pam_conv conversation = { pam_conv_cb, session };

    pam_start (session->priv->service, session->priv->username, &conversation, &session->priv->pam_handle);
    session->priv->result = pam_authenticate (session->priv->pam_handle, 0);

    g_debug ("pam_authenticate -> %s", pam_strerror (session->priv->pam_handle, session->priv->result));

    /* Notify user */
    g_idle_add (notify_auth_complete_cb, session);

    return NULL;
}

gboolean
pam_session_start (PAMSession *session, GError **error)
{
    g_return_val_if_fail (session->priv->authentication_thread == NULL, FALSE);

    g_signal_emit (G_OBJECT (session), signals[AUTHENTICATION_STARTED], 0);
  
    /* Hold a reference to this object while the thread may access it */
    g_object_ref (session);

    /* Start thread */
    session->priv->authentication_response_queue = g_async_queue_new ();
    session->priv->authentication_thread = g_thread_create (authenticate_cb, session, TRUE, error);
    if (!session->priv->authentication_thread)
        return FALSE;

    return TRUE;
}

const gchar *
pam_session_strerror (PAMSession *session, int error)
{
    return pam_strerror (session->priv->pam_handle, error);
}

const gchar *
pam_session_get_username (PAMSession *session)
{
    return session->priv->username;
}

const struct pam_message **
pam_session_get_messages (PAMSession *session)
{
    return session->priv->messages;  
}

gint
pam_session_get_num_messages (PAMSession *session)
{
    return session->priv->num_messages;
}

void
pam_session_respond (PAMSession *session, struct pam_response *response)
{
    g_return_if_fail (session->priv->authentication_thread != NULL);  
    g_async_queue_push (session->priv->authentication_response_queue, response);
}

void
pam_session_cancel (PAMSession *session)
{
    g_return_if_fail (session->priv->authentication_thread != NULL);
    session->priv->cancel = TRUE;
    g_async_queue_push (session->priv->authentication_response_queue, GINT_TO_POINTER (-1));
}

const gchar *
pam_session_getenv (PAMSession *session, const gchar *name)
{
    return pam_getenv (session->priv->pam_handle, name);
}

gchar **
pam_session_get_envlist (PAMSession *session)
{
    return pam_getenvlist (session->priv->pam_handle);
}

void
pam_session_end (PAMSession *session)
{ 
    /* If authenticating cancel first */
    if (session->priv->authentication_thread)
    {
        session->priv->stop = TRUE;
        g_async_queue_push (session->priv->authentication_response_queue, GINT_TO_POINTER (-1));
    }
    else if (session->priv->in_session)
    {
        pam_close_session (session->priv->pam_handle, 0);
        session->priv->in_session = FALSE;
        g_signal_emit (G_OBJECT (session), signals[ENDED], 0);
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
  
    pam_session_end (self);

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

    signals[ENDED] =
        g_signal_new ("ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PAMSessionClass, ended),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);
}
