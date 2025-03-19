/*
 * Copyright (C) 2010-2011 David Edmundson
 * Copyright (C) 2010-2011 Robert Ancell
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */


#include "QLightDM/greeter.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QVariant>
#include <QtCore/QSettings>

#include <lightdm.h>

using namespace QLightDM;

class QLightDM::GreeterPrivate
{
public:
    GreeterPrivate(Greeter *parent);
    LightDMGreeter *ldmGreeter;
protected:
    Greeter* q_ptr;

    static void cb_showPrompt(LightDMGreeter *greeter, const gchar *text, LightDMPromptType type, gpointer data);
    static void cb_showMessage(LightDMGreeter *greeter, const gchar *text, LightDMMessageType type, gpointer data);
    static void cb_authenticationComplete(LightDMGreeter *greeter, gpointer data);
    static void cb_autoLoginExpired(LightDMGreeter *greeter, gpointer data);
    static void cb_idle(LightDMGreeter *greeter, gpointer data);
    static void cb_reset(LightDMGreeter *greeter, gpointer data);

private:
    Q_DECLARE_PUBLIC(Greeter)
};

GreeterPrivate::GreeterPrivate(Greeter *parent) :
    q_ptr(parent)
{
#if !defined(GLIB_VERSION_2_36)
    g_type_init();
#endif
    ldmGreeter = lightdm_greeter_new();

    g_signal_connect (ldmGreeter, LIGHTDM_GREETER_SIGNAL_SHOW_PROMPT, G_CALLBACK (cb_showPrompt), this);
    g_signal_connect (ldmGreeter, LIGHTDM_GREETER_SIGNAL_SHOW_MESSAGE, G_CALLBACK (cb_showMessage), this);
    g_signal_connect (ldmGreeter, LIGHTDM_GREETER_SIGNAL_AUTHENTICATION_COMPLETE, G_CALLBACK (cb_authenticationComplete), this);
    g_signal_connect (ldmGreeter, LIGHTDM_GREETER_SIGNAL_AUTOLOGIN_TIMER_EXPIRED, G_CALLBACK (cb_autoLoginExpired), this);
    g_signal_connect (ldmGreeter, LIGHTDM_GREETER_SIGNAL_IDLE, G_CALLBACK (cb_idle), this);
    g_signal_connect (ldmGreeter, LIGHTDM_GREETER_SIGNAL_RESET, G_CALLBACK (cb_reset), this);
}

void GreeterPrivate::cb_showPrompt(LightDMGreeter *greeter, const gchar *text, LightDMPromptType type, gpointer data)
{
    Q_UNUSED(greeter);

    GreeterPrivate *that = static_cast<GreeterPrivate*>(data);
    QString message = QString::fromUtf8(text);

    Q_EMIT that->q_func()->showPrompt(message, type == LIGHTDM_PROMPT_TYPE_QUESTION ?
                                               Greeter::PromptTypeQuestion : Greeter::PromptTypeSecret);
}

void GreeterPrivate::cb_showMessage(LightDMGreeter *greeter, const gchar *text, LightDMMessageType type, gpointer data)
{
    Q_UNUSED(greeter);

    GreeterPrivate *that = static_cast<GreeterPrivate*>(data);
    QString message = QString::fromUtf8(text);

    Q_EMIT that->q_func()->showMessage(message, type == LIGHTDM_MESSAGE_TYPE_INFO ?
                                                Greeter::MessageTypeInfo : Greeter::MessageTypeError);
}

void GreeterPrivate::cb_authenticationComplete(LightDMGreeter *greeter, gpointer data)
{
    Q_UNUSED(greeter);
    GreeterPrivate *that = static_cast<GreeterPrivate*>(data);
    Q_EMIT that->q_func()->authenticationComplete();
}

void GreeterPrivate::cb_autoLoginExpired(LightDMGreeter *greeter, gpointer data)
{
    Q_UNUSED(greeter);
    GreeterPrivate *that = static_cast<GreeterPrivate*>(data);
    Q_EMIT that->q_func()->autologinTimerExpired();
}

void GreeterPrivate::cb_idle(LightDMGreeter *greeter, gpointer data)
{
    Q_UNUSED(greeter);
    GreeterPrivate *that = static_cast<GreeterPrivate*>(data);
    Q_EMIT that->q_func()->idle();
}

void GreeterPrivate::cb_reset(LightDMGreeter *greeter, gpointer data)
{
    Q_UNUSED(greeter);
    GreeterPrivate *that = static_cast<GreeterPrivate*>(data);
    Q_EMIT that->q_func()->reset();
}

Greeter::Greeter(QObject *parent) :
    QObject(parent),
    d_ptr(new GreeterPrivate(this))
{
}

Greeter::~Greeter()
{
    delete d_ptr;
}


bool Greeter::connectToDaemonSync()
{
    Q_D(Greeter);
    return lightdm_greeter_connect_to_daemon_sync(d->ldmGreeter, NULL);
}

bool Greeter::connectSync()
{
    Q_D(Greeter);
    return lightdm_greeter_connect_to_daemon_sync(d->ldmGreeter, NULL);
}

void Greeter::authenticate(const QString &username)
{
    Q_D(Greeter);
    lightdm_greeter_authenticate(d->ldmGreeter, username.toLocal8Bit().data(), NULL);
}

void Greeter::authenticateAsGuest()
{
    Q_D(Greeter);
    lightdm_greeter_authenticate_as_guest(d->ldmGreeter, NULL);
}

void Greeter::authenticateAutologin()
{
    Q_D(Greeter);
    lightdm_greeter_authenticate_autologin(d->ldmGreeter, NULL);
}

void Greeter::authenticateRemote(const QString &session, const QString &username)
{
    Q_D(Greeter);
    lightdm_greeter_authenticate_remote(d->ldmGreeter, session.toLocal8Bit().data(), username.toLocal8Bit().data(), NULL);
}

void Greeter::respond(const QString &response)
{
    Q_D(Greeter);
    lightdm_greeter_respond(d->ldmGreeter, response.toLocal8Bit().data(), NULL);
}

void Greeter::cancelAuthentication()
{
    Q_D(Greeter);
    lightdm_greeter_cancel_authentication(d->ldmGreeter, NULL);
}

void Greeter::cancelAutologin()
{
    Q_D(Greeter);
    lightdm_greeter_cancel_autologin(d->ldmGreeter);
}

bool Greeter::inAuthentication() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_in_authentication(d->ldmGreeter);
}

bool Greeter::isAuthenticated() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_is_authenticated(d->ldmGreeter);
}

QString Greeter::authenticationUser() const
{
    Q_D(const Greeter);
    return QString::fromUtf8(lightdm_greeter_get_authentication_user(d->ldmGreeter));
}

void Greeter::setLanguage (const QString &language)
{
    Q_D(Greeter);
    lightdm_greeter_set_language(d->ldmGreeter, language.toLocal8Bit().constData(), NULL);
}

void Greeter::setResettable (bool resettable)
{
    Q_D(Greeter);
    lightdm_greeter_set_resettable(d->ldmGreeter, resettable);
}

bool Greeter::startSessionSync(const QString &session)
{
    Q_D(Greeter);
    return lightdm_greeter_start_session_sync(d->ldmGreeter, session.toLocal8Bit().constData(), NULL);
}

QString Greeter::ensureSharedDataDirSync(const QString &username)
{
    Q_D(Greeter);
    return QString::fromUtf8(lightdm_greeter_ensure_shared_data_dir_sync(d->ldmGreeter, username.toLocal8Bit().constData(), NULL));
}


QString Greeter::getHint(const QString &name) const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_hint(d->ldmGreeter, name.toLocal8Bit().constData());
}

QString Greeter::defaultSessionHint() const
{
    Q_D(const Greeter);
    return QString::fromUtf8(lightdm_greeter_get_default_session_hint(d->ldmGreeter));
}

bool Greeter::hideUsersHint() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_hide_users_hint(d->ldmGreeter);
}

bool Greeter::showManualLoginHint() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_show_manual_login_hint(d->ldmGreeter);
}

bool Greeter::showRemoteLoginHint() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_show_remote_login_hint(d->ldmGreeter);
}

bool Greeter::lockHint() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_lock_hint(d->ldmGreeter);
}

bool Greeter::hasGuestAccountHint() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_has_guest_account_hint(d->ldmGreeter);
}

QString Greeter::selectUserHint() const
{
    Q_D(const Greeter);
    return QString::fromUtf8(lightdm_greeter_get_select_user_hint(d->ldmGreeter));
}

bool Greeter::selectGuestHint() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_select_guest_hint(d->ldmGreeter);
}

QString Greeter::autologinUserHint() const
{
    Q_D(const Greeter);
    return QString::fromUtf8(lightdm_greeter_get_autologin_user_hint(d->ldmGreeter));
}

QString Greeter::autologinSessionHint() const
{
    Q_D(const Greeter);
    return QString::fromUtf8(lightdm_greeter_get_autologin_session_hint(d->ldmGreeter));
}

bool Greeter::autologinGuestHint() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_autologin_guest_hint(d->ldmGreeter);
}

int Greeter::autologinTimeoutHint() const
{
    Q_D(const Greeter);
    return lightdm_greeter_get_autologin_timeout_hint(d->ldmGreeter);
}

QString Greeter::hostname() const
{
    return QString::fromUtf8(lightdm_get_hostname());
}

QString Greeter::osName() const
{
    return QString::fromUtf8(lightdm_get_os_name());
}

QString Greeter::osId() const
{
    return QString::fromUtf8(lightdm_get_os_id());
}

QString Greeter::osPrettyName() const
{
    return QString::fromUtf8(lightdm_get_os_pretty_name());
}

QString Greeter::osVersion() const
{
    return QString::fromUtf8(lightdm_get_os_version());
}

QString Greeter::osVersionId() const
{
    return QString::fromUtf8(lightdm_get_os_version_id());
}

QString Greeter::motd() const
{
    return QString::fromUtf8(lightdm_get_motd());
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include "greeter_moc6.cpp"
#else
#include "greeter_moc5.cpp"
#endif
