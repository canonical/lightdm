/*
 * Copyright (C) 2010-2011 David Edmundson
 * Copyright (C) 2010-2011 Robert Ancell
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */


#include "QLightDM/greeter.h"

#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QVariant>
#include <QtCore/QSettings>

#include <lightdm-gobject-1/lightdm.h>

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
    
private:
    Q_DECLARE_PUBLIC(Greeter)
};

GreeterPrivate::GreeterPrivate(Greeter *parent) :
    q_ptr(parent)
{
    g_type_init();
    ldmGreeter = lightdm_greeter_new();
    
    g_signal_connect (ldmGreeter, "show-prompt", G_CALLBACK (cb_showPrompt), this);
    g_signal_connect (ldmGreeter, "show-message", G_CALLBACK (cb_showMessage), this);
    g_signal_connect (ldmGreeter, "authentication-complete", G_CALLBACK (cb_authenticationComplete), this);
    g_signal_connect (ldmGreeter, "autologin-timer-expired", G_CALLBACK (cb_autoLoginExpired), this);
}

void GreeterPrivate::cb_showPrompt(LightDMGreeter *greeter, const gchar *text, LightDMPromptType type, gpointer data)
{
    Q_UNUSED(greeter);
    
    GreeterPrivate *that = static_cast<GreeterPrivate*>(data);
    QString message = QString::fromLocal8Bit(text);
    
    //FIXME prompt type

    Q_EMIT that->q_func()->showPrompt(message, Greeter::PromptTypeSecret);
}

void GreeterPrivate::cb_showMessage(LightDMGreeter *greeter, const gchar *text, LightDMMessageType type, gpointer data)
{
    Q_UNUSED(greeter);

    GreeterPrivate *that = static_cast<GreeterPrivate*>(data);
    QString message = QString::fromLocal8Bit(text);

    //FIXME prompt type

    Q_EMIT that->q_func()->showMessage(message, Greeter::MessageTypeInfo);
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

Greeter::Greeter(QObject *parent) :
    QObject(parent),
    d_ptr(new GreeterPrivate(this))
{
}

Greeter::~Greeter()
{
    delete d_ptr;
}


bool Greeter::connectSync()
{
    Q_D(Greeter);
    return lightdm_greeter_connect_sync(d->ldmGreeter, NULL);
}

void Greeter::authenticate(const QString &username)
{
    Q_D(Greeter);
    lightdm_greeter_authenticate(d->ldmGreeter, username.toLocal8Bit().data());
}

void Greeter::authenticateAsGuest()
{
    Q_D(Greeter);
    lightdm_greeter_authenticate_as_guest(d->ldmGreeter);
    
}

void Greeter::respond(const QString &response)
{
    Q_D(Greeter);
    lightdm_greeter_respond(d->ldmGreeter, response.toLocal8Bit().data());
}

void Greeter::cancelAuthentication()
{
    Q_D(Greeter);
    lightdm_greeter_cancel_authentication(d->ldmGreeter);
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
    const gchar* string = lightdm_greeter_get_authentication_user(d->ldmGreeter);
    QString authenticationUser = QString::fromLocal8Bit(string);
    return authenticationUser;
}

void Greeter::setLanguage (const QString &language)
{
}

bool Greeter::startSessionSync(const QString &session)
{
    Q_D(Greeter);
    return lightdm_greeter_start_session_sync(d->ldmGreeter, session.toLocal8Bit().constData(), NULL);
}


QString Greeter::getHint(const QString &name) const
{
}

QString Greeter::defaultSessionHint() const
{
}

bool Greeter::hideUsersHint() const
{
}

bool Greeter::hasGuestAccountHint() const
{
}

QString Greeter::selectUserHint() const
{
}

bool Greeter::selectGuestHint() const
{
}

QString Greeter::autologinUserHint() const
{
}

bool Greeter::autologinGuestHint() const
{
}

int Greeter::autologinTimeoutHint() const
{
}

#include "greeter_moc.cpp"
