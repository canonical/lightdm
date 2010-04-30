/*
 * Copyright (C) 2010 Canonical Ltd.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include "pam-authenticator.h"

enum {
    GOT_MESSAGES,
    AUTHENTICATION_COMPLETE,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct PAMAuthenticatorPrivate
{
    /* User being authenticated */
    char *username;

    /* Authentication thread */
    GThread *authentication_thread;

    /* Messages requested */
    int num_messages;
    const struct pam_message **messages;
    int result;

    /* Queue to feed responses to the authentication thread */
    GAsyncQueue *authentication_response_queue;

    /* Authentication handle */
    pam_handle_t *pam_handle;
};

G_DEFINE_TYPE (PAMAuthenticator, pam_authenticator, G_TYPE_OBJECT);

PAMAuthenticator *
pam_authenticator_new (void)
{
    return g_object_new (PAM_AUTHENTICATOR_TYPE, NULL);
}

static gboolean
notify_messages_cb (gpointer data)
{
    PAMAuthenticator *authenticator = data;

    g_signal_emit (G_OBJECT (authenticator), signals[GOT_MESSAGES], 0, authenticator->priv->num_messages, authenticator->priv->messages);

    return FALSE;
}

static int
pam_conv_cb (int num_msg, const struct pam_message **msg,
             struct pam_response **resp, void *app_data)
{
    PAMAuthenticator *authenticator = app_data;

    /* Notify user */
    authenticator->priv->num_messages = num_msg;
    authenticator->priv->messages = msg;
    g_idle_add (notify_messages_cb, authenticator);

    /*  wait for response */  
    *resp = g_async_queue_pop (authenticator->priv->authentication_response_queue);
    if (*resp == NULL)
        return PAM_SYSTEM_ERR;

    authenticator->priv->num_messages = 0;
    authenticator->priv->messages = NULL;

    return PAM_SUCCESS;
}

static gboolean
notify_auth_complete_cb (gpointer data)
{
    PAMAuthenticator *authenticator = data;
    int result;

    result = authenticator->priv->result;
    authenticator->priv->result = 0;
    g_free (authenticator->priv->username);
    authenticator->priv->username = NULL;

    g_thread_join (authenticator->priv->authentication_thread);
    authenticator->priv->authentication_thread = NULL;
    g_async_queue_unref (authenticator->priv->authentication_response_queue);
    authenticator->priv->authentication_response_queue = NULL;

    g_signal_emit (G_OBJECT (authenticator), signals[AUTHENTICATION_COMPLETE], 0, result);

    return FALSE;
}

static gpointer
authenticate_cb (gpointer data)
{
    PAMAuthenticator *authenticator = data;
    struct pam_conv conversation = { pam_conv_cb, authenticator };

    pam_start ("check_pass", authenticator->priv->username, &conversation, &authenticator->priv->pam_handle);
    authenticator->priv->result = pam_authenticate (authenticator->priv->pam_handle, 0);
    g_debug ("pam_authenticate -> %s", pam_strerror (authenticator->priv->pam_handle, authenticator->priv->result));

    /* Notify user */
    g_idle_add (notify_auth_complete_cb, authenticator);

    return NULL;
}

gboolean
pam_authenticator_start (PAMAuthenticator *authenticator, const char *username, GError **error)
{
    g_return_val_if_fail (authenticator->priv->authentication_thread == NULL, FALSE);

    g_free (authenticator->priv->username);
    authenticator->priv->username = g_strdup (username);

    /* Start thread */
    authenticator->priv->authentication_response_queue = g_async_queue_new ();
    authenticator->priv->authentication_thread = g_thread_create (authenticate_cb, authenticator, TRUE, error);
    if (!authenticator->priv->authentication_thread)
        return FALSE;

    return TRUE;
}

const struct pam_message **
pam_authenticator_get_messages (PAMAuthenticator *authenticator)
{
    return authenticator->priv->messages;  
}

int
pam_authenticator_get_num_messages (PAMAuthenticator *authenticator)
{
    return authenticator->priv->num_messages;
}

void
pam_authenticator_cancel (PAMAuthenticator *authenticator)
{
    g_return_if_fail (authenticator->priv->authentication_thread != NULL);
    g_async_queue_push (authenticator->priv->authentication_response_queue, NULL);
}

void
pam_authenticator_respond (PAMAuthenticator *authenticator, struct pam_response *response)
{
    g_return_if_fail (authenticator->priv->authentication_thread != NULL);  
    g_async_queue_push (authenticator->priv->authentication_response_queue, response);
}

static void
pam_authenticator_init (PAMAuthenticator *authenticator)
{
    authenticator->priv = G_TYPE_INSTANCE_GET_PRIVATE (authenticator, PAM_AUTHENTICATOR_TYPE, PAMAuthenticatorPrivate);
}

/* Generated with glib-genmarshal */
static void
g_cclosure_user_marshal_VOID__INT_POINTER (GClosure     *closure,
                                           GValue       *return_value G_GNUC_UNUSED,
                                           guint         n_param_values,
                                           const GValue *param_values,
                                           gpointer      invocation_hint G_GNUC_UNUSED,
                                           gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__INT_POINTER) (gpointer       data1,
                                                  gint           arg_1,
                                                  gconstpointer  arg_2,
                                                  gpointer       data2);
  register GMarshalFunc_VOID__INT_POINTER callback;
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
  callback = (GMarshalFunc_VOID__INT_POINTER) (marshal_data ? marshal_data : cc->callback);

  callback (data1,
            g_value_get_int (param_values + 1),
            g_value_get_pointer (param_values + 2),
            data2);
}

static void
pam_authenticator_class_init (PAMAuthenticatorClass *klass)
{
    g_type_class_add_private (klass, sizeof (PAMAuthenticatorPrivate));

    signals[GOT_MESSAGES] =
        g_signal_new ("got-messages",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PAMAuthenticatorClass, got_messages),
                      NULL, NULL,
                      g_cclosure_user_marshal_VOID__INT_POINTER,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);
    signals[AUTHENTICATION_COMPLETE] =
        g_signal_new ("authentication-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (PAMAuthenticatorClass, authentication_complete),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__INT,
                      G_TYPE_NONE, 1, G_TYPE_INT);
}
