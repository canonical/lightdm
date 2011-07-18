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

#ifndef _LDM_GREETER_H_
#define _LDM_GREETER_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define LDM_TYPE_GREETER            (ldm_greeter_get_type())
#define LDM_GREETER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), LDM_TYPE_GREETER, LdmGreeter));
#define LDM_GREETER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), LDM_TYPE_GREETER, LdmGreeterClass))
#define LDM_IS_GREETER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LDM_TYPE_GREETER))
#define LDM_IS_GREETER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LDM_TYPE_GREETER))
#define LDM_GREETER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), LDM_TYPE_GREETER, LdmGreeterClass))

typedef struct _LdmGreeter        LdmGreeter;
typedef struct _LdmGreeterClass   LdmGreeterClass;

#include "user.h"
#include "language.h"
#include "layout.h"
#include "session.h"

/**
 * LdmPromptType:
 * @LDM_PROMPT_TYPE_QUESTION: Prompt is a question.  The information can be shown as it is entered.
 * @LDM_PROMPT_TYPE_SECRET: Prompt is for secret information.  The entered information should be obscured so it can't be publically visible.
 */
typedef enum
{
    LDM_PROMPT_TYPE_QUESTION,
    LDM_PROMPT_TYPE_SECRET
} LdmPromptType;

/**
 * LdmMessageType:
 * @LDM_MESSAGE_TYPE_INFO: Informational message.
 * @LDM_MESSAGE_TYPE_ERROR: Error message.
 */
typedef enum
{
    LDM_MESSAGE_TYPE_INFO,
    LDM_MESSAGE_TYPE_ERROR
} LdmMessageType;

struct _LdmGreeter
{
    GObject parent_instance;
};

struct _LdmGreeterClass
{
    GObjectClass parent_class;

    void (*connected)(LdmGreeter *greeter);
    void (*show_prompt)(LdmGreeter *greeter, const gchar *text, LdmPromptType type);
    void (*show_message)(LdmGreeter *greeter, const gchar *text, LdmMessageType type);
    void (*authentication_complete)(LdmGreeter *greeter);
    void (*session_failed)(LdmGreeter *greeter);
    void (*autologin_timer_expired)(LdmGreeter *greeter);
    void (*user_added)(LdmGreeter *greeter, LdmUser *user);
    void (*user_changed)(LdmGreeter *greeter, LdmUser *user);
    void (*user_removed)(LdmGreeter *greeter, LdmUser *user);
    void (*quit)(LdmGreeter *greeter);
};

GType ldm_greeter_get_type (void);

LdmGreeter *ldm_greeter_new (void);

gboolean ldm_greeter_connect_to_server (LdmGreeter *greeter);

const gchar *ldm_greeter_get_hostname (LdmGreeter *greeter);

gint ldm_greeter_get_num_users (LdmGreeter *greeter);

GList *ldm_greeter_get_users (LdmGreeter *greeter);

LdmUser *ldm_greeter_get_user_by_name (LdmGreeter *greeter, const gchar *username);

const gchar *ldm_greeter_get_default_language (LdmGreeter *greeter);

GList *ldm_greeter_get_languages (LdmGreeter *greeter);

GList *ldm_greeter_get_layouts (LdmGreeter *greeter);

void ldm_greeter_set_layout (LdmGreeter *greeter, const gchar *layout);

const gchar *ldm_greeter_get_layout (LdmGreeter *greeter);

GList *ldm_greeter_get_sessions (LdmGreeter *greeter);

const gchar *ldm_greeter_get_hint (LdmGreeter *greeter, const gchar *name);

const gchar *ldm_greeter_get_default_session_hint (LdmGreeter *greeter);

gboolean ldm_greeter_get_hide_users_hint (LdmGreeter *greeter);

gboolean ldm_greeter_get_has_guest_account_hint (LdmGreeter *greeter);

const gchar *ldm_greeter_get_select_user_hint (LdmGreeter *greeter);

gboolean ldm_greeter_get_select_guest_hint (LdmGreeter *greeter);

const gchar *ldm_greeter_get_autologin_user_hint (LdmGreeter *greeter);

gboolean ldm_greeter_get_autologin_guest_hint (LdmGreeter *greeter);

gint ldm_greeter_get_autologin_timeout_hint (LdmGreeter *greeter);

void ldm_greeter_cancel_timed_login (LdmGreeter *greeter);

void ldm_greeter_login (LdmGreeter *greeter, const char *username);

void ldm_greeter_login_with_user_prompt (LdmGreeter *greeter);

void ldm_greeter_login_as_guest (LdmGreeter *greeter);

void ldm_greeter_respond (LdmGreeter *greeter, const gchar *response);

void ldm_greeter_cancel_authentication (LdmGreeter *greeter);

gboolean ldm_greeter_get_in_authentication (LdmGreeter *greeter);

gboolean ldm_greeter_get_is_authenticated (LdmGreeter *greeter);

const gchar *ldm_greeter_get_authentication_user (LdmGreeter *greeter);

void ldm_greeter_start_session (LdmGreeter *greeter, const gchar *session);

void ldm_greeter_start_default_session (LdmGreeter *greeter);

gboolean ldm_greeter_get_can_suspend (LdmGreeter *greeter);

void ldm_greeter_suspend (LdmGreeter *greeter);

gboolean ldm_greeter_get_can_hibernate (LdmGreeter *greeter);

void ldm_greeter_hibernate (LdmGreeter *greeter);

gboolean ldm_greeter_get_can_restart (LdmGreeter *greeter);

void ldm_greeter_restart (LdmGreeter *greeter);

gboolean ldm_greeter_get_can_shutdown (LdmGreeter *greeter);

void ldm_greeter_shutdown (LdmGreeter *greeter);

G_END_DECLS

#endif /* _LDM_GREETER_H_ */
