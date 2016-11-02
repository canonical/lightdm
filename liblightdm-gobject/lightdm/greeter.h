/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef LIGHTDM_GREETER_H_
#define LIGHTDM_GREETER_H_

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define LIGHTDM_TYPE_GREETER            (lightdm_greeter_get_type())
#define LIGHTDM_GREETER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIGHTDM_TYPE_GREETER, LightDMGreeter))
#define LIGHTDM_GREETER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LIGHTDM_TYPE_GREETER, LightDMGreeterClass))
#define LIGHTDM_IS_GREETER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIGHTDM_TYPE_GREETER))
#define LIGHTDM_IS_GREETER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LIGHTDM_TYPE_GREETER))
#define LIGHTDM_GREETER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LIGHTDM_TYPE_GREETER, LightDMGreeterClass))

typedef struct _LightDMGreeter          LightDMGreeter;
typedef struct _LightDMGreeterClass     LightDMGreeterClass;

#define LIGHTDM_GREETER_ERROR lightdm_greeter_error_quark ()

#define LIGHTDM_GREETER_SIGNAL_SHOW_PROMPT             "show-prompt"
#define LIGHTDM_GREETER_SIGNAL_SHOW_MESSAGE            "show-message"
#define LIGHTDM_GREETER_SIGNAL_AUTHENTICATION_COMPLETE "authentication-complete"
#define LIGHTDM_GREETER_SIGNAL_AUTOLOGIN_TIMER_EXPIRED "autologin-timer-expired"
#define LIGHTDM_GREETER_SIGNAL_IDLE                    "idle"
#define LIGHTDM_GREETER_SIGNAL_RESET                   "reset"

/**
 * LightDMPromptType:
 * @LIGHTDM_PROMPT_TYPE_QUESTION: Prompt is a question.  The information can be shown as it is entered.
 * @LIGHTDM_PROMPT_TYPE_SECRET: Prompt is for secret information.  The entered information should be obscured so it can't be publically visible.
 */
typedef enum
{
    LIGHTDM_PROMPT_TYPE_QUESTION,
    LIGHTDM_PROMPT_TYPE_SECRET
} LightDMPromptType;

GType lightdm_prompt_type_get_type (void);

/**
 * LightDMMessageType:
 * @LIGHTDM_MESSAGE_TYPE_INFO: Informational message.
 * @LIGHTDM_MESSAGE_TYPE_ERROR: Error message.
 */
typedef enum
{
    LIGHTDM_MESSAGE_TYPE_INFO,
    LIGHTDM_MESSAGE_TYPE_ERROR
} LightDMMessageType;

GType lightdm_message_type_get_type (void);

struct _LightDMGreeter
{
    GObject parent_instance;
};

struct _LightDMGreeterClass
{
    GObjectClass parent_class;

    void (*show_message)(LightDMGreeter *greeter, const gchar *text, LightDMMessageType type);
    void (*show_prompt)(LightDMGreeter *greeter, const gchar *text, LightDMPromptType type);
    void (*authentication_complete)(LightDMGreeter *greeter);
    void (*autologin_timer_expired)(LightDMGreeter *greeter);
    void (*idle)(LightDMGreeter *greeter);
    void (*reset)(LightDMGreeter *greeter);

    /* Reserved */
    void (*reserved1) (void);
    void (*reserved2) (void);
    void (*reserved3) (void);
    void (*reserved4) (void);
};

#ifdef GLIB_VERSION_2_44
typedef LightDMGreeter *LightDMGreeter_autoptr;
static inline void glib_autoptr_cleanup_LightDMGreeter (LightDMGreeter **_ptr)
{
    glib_autoptr_cleanup_GObject ((GObject **) _ptr);
}
#endif

/**
 * LightDMGreeterError:
 * @LIGHTDM_GREETER_ERROR_COMMUNICATION_ERROR: error communicating with daemon.
 * @LIGHTDM_GREETER_ERROR_CONNECTION_FAILED: failed to connect to the daemon.
 * @LIGHTDM_GREETER_ERROR_SESSION_FAILED: requested session failed to start.
 * @LIGHTDM_GREETER_ERROR_NO_AUTOLOGIN: autologin not configured.
 * @LIGHTDM_GREETER_ERROR_INVALID_USER: autologin not configured.
 *
 * Error codes returned by greeter operations.
 */
typedef enum
{
    LIGHTDM_GREETER_ERROR_COMMUNICATION_ERROR,
    LIGHTDM_GREETER_ERROR_CONNECTION_FAILED,
    LIGHTDM_GREETER_ERROR_SESSION_FAILED,
    LIGHTDM_GREETER_ERROR_NO_AUTOLOGIN,
    LIGHTDM_GREETER_ERROR_INVALID_USER
} LightDMGreeterError;

GQuark lightdm_greeter_error_quark (void);

GType lightdm_greeter_error_get_type (void);

GType lightdm_greeter_get_type (void);

LightDMGreeter *lightdm_greeter_new (void);

void lightdm_greeter_set_resettable (LightDMGreeter *greeter, gboolean resettable);

void lightdm_greeter_connect_to_daemon (LightDMGreeter *greeter, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);

gboolean lightdm_greeter_connect_to_daemon_finish (LightDMGreeter *greeter, GAsyncResult *result, GError **error);

gboolean lightdm_greeter_connect_to_daemon_sync (LightDMGreeter *greeter, GError **error);

const gchar *lightdm_greeter_get_hint (LightDMGreeter *greeter, const gchar *name);

const gchar *lightdm_greeter_get_default_session_hint (LightDMGreeter *greeter);

gboolean lightdm_greeter_get_hide_users_hint (LightDMGreeter *greeter);

gboolean lightdm_greeter_get_show_manual_login_hint (LightDMGreeter *greeter);

gboolean lightdm_greeter_get_show_remote_login_hint (LightDMGreeter *greeter);

gboolean lightdm_greeter_get_lock_hint (LightDMGreeter *greeter);

gboolean lightdm_greeter_get_has_guest_account_hint (LightDMGreeter *greeter);

const gchar *lightdm_greeter_get_select_user_hint (LightDMGreeter *greeter);

gboolean lightdm_greeter_get_select_guest_hint (LightDMGreeter *greeter);

const gchar *lightdm_greeter_get_autologin_user_hint (LightDMGreeter *greeter);

gboolean lightdm_greeter_get_autologin_guest_hint (LightDMGreeter *greeter);

gint lightdm_greeter_get_autologin_timeout_hint (LightDMGreeter *greeter);

void lightdm_greeter_cancel_autologin (LightDMGreeter *greeter);

gboolean lightdm_greeter_authenticate (LightDMGreeter *greeter, const gchar *username, GError **error);

gboolean lightdm_greeter_authenticate_as_guest (LightDMGreeter *greeter, GError **error);

gboolean lightdm_greeter_authenticate_autologin (LightDMGreeter *greeter, GError **error);

gboolean lightdm_greeter_authenticate_remote (LightDMGreeter *greeter, const gchar *session, const gchar *username, GError **error);

gboolean lightdm_greeter_respond (LightDMGreeter *greeter, const gchar *response, GError **error);

gboolean lightdm_greeter_cancel_authentication (LightDMGreeter *greeter, GError **error);

gboolean lightdm_greeter_get_in_authentication (LightDMGreeter *greeter);

gboolean lightdm_greeter_get_is_authenticated (LightDMGreeter *greeter);

const gchar *lightdm_greeter_get_authentication_user (LightDMGreeter *greeter);

gboolean lightdm_greeter_set_language (LightDMGreeter *greeter, const gchar *language, GError **error);

void lightdm_greeter_start_session (LightDMGreeter *greeter, const gchar *session, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);

gboolean lightdm_greeter_start_session_finish (LightDMGreeter *greeter, GAsyncResult *result, GError **error);

gboolean lightdm_greeter_start_session_sync (LightDMGreeter *greeter, const gchar *session, GError **error);

void lightdm_greeter_ensure_shared_data_dir (LightDMGreeter *greeter, const gchar *username, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);

gchar *lightdm_greeter_ensure_shared_data_dir_finish (LightDMGreeter *greeter, GAsyncResult *result, GError **error);

gchar *lightdm_greeter_ensure_shared_data_dir_sync (LightDMGreeter *greeter, const gchar *username, GError **error);

#ifndef LIGHTDM_DISABLE_DEPRECATED
gboolean lightdm_greeter_connect_sync (LightDMGreeter *greeter, GError **error);
#endif

G_END_DECLS

#endif /* LIGHTDM_GREETER_H_ */
