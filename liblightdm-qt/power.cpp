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


#include "QLightDM/power.h"

#include <QtCore/QVariant>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>
#include <QDebug>

#include "config.h"

using namespace QLightDM;

class PowerInterface::PowerInterfacePrivate
{
public:
    PowerInterfacePrivate();
    QScopedPointer<QDBusInterface> powerManagementInterface;
    QScopedPointer<QDBusInterface> consoleKitInterface;
    QScopedPointer<QDBusInterface> login1Interface;
};

PowerInterface::PowerInterfacePrivate::PowerInterfacePrivate() :
    powerManagementInterface(new QDBusInterface("org.freedesktop.UPower","/org/freedesktop/UPower", "org.freedesktop.UPower", QDBusConnection::systemBus())),
    consoleKitInterface(new QDBusInterface("org.freedesktop.ConsoleKit", "/org/freedesktop/ConsoleKit/Manager", "org.freedesktop.ConsoleKit.Manager", QDBusConnection::systemBus())),
    login1Interface(new QDBusInterface("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", QDBusConnection::systemBus()))
{
}


PowerInterface::PowerInterface(QObject *parent)
    : QObject(parent),
      d(new PowerInterfacePrivate)
{
}

PowerInterface::~PowerInterface()
{
    delete d;
}

bool PowerInterface::canSuspend()
{
    QDBusReply<bool> reply = d->powerManagementInterface->call("SuspendAllowed");
    if (reply.isValid()) {
        return reply.value();
    }
    else {
        return false;
    }
}

void PowerInterface::suspend()
{
    d->powerManagementInterface->call("Suspend");
}

bool PowerInterface::canHibernate()
{
    QDBusReply<bool> reply = d->powerManagementInterface->call("HibernateAllowed");
    if (reply.isValid()) {
        return reply.value();
    }
    else {
        return false;
    }
}

void PowerInterface::hibernate()
{
    d->powerManagementInterface->call("Hibernate");
}

bool PowerInterface::canShutdown()
{
    if (d->login1Interface->isValid()) {
        QDBusReply<QString> reply1 = d->login1Interface->call("CanPowerOff");
        if (reply1.isValid()) {
            return reply1.value() == "yes";
        }
    }
    qWarning() << d->login1Interface->lastError();

    QDBusReply<bool> reply = d->consoleKitInterface->call("CanStop");
    if (reply.isValid()) {
        return reply.value();
    }

    return false;
}

void PowerInterface::shutdown()
{
    if (d->login1Interface->isValid())
        d->login1Interface->call("PowerOff", false);
    else
        d->consoleKitInterface->call("Stop");
}

bool PowerInterface::canRestart()
{
    if (d->login1Interface->isValid()) {
        QDBusReply<QString> reply1 = d->login1Interface->call("CanReboot");
        if (reply1.isValid()) {
            return reply1.value() == "yes";
        }
    }
    qWarning() << d->login1Interface->lastError();
  
    QDBusReply<bool> reply = d->consoleKitInterface->call("CanRestart");
    if (reply.isValid()) {
        return reply.value();
    }

    return false;
}

void PowerInterface::restart()
{
    if (d->login1Interface->isValid())
        d->login1Interface->call("Reboot", false);
    else
        d->consoleKitInterface->call("Restart");
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include "power_moc5.cpp"
#else
#include "power_moc4.cpp"
#endif
