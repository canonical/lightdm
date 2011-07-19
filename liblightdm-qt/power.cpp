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

#include "config.h"

#include "QLightDM/Power"

#include <QtCore/QVariant>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>

using namespace QLightDM;

static QDBusInterface* powerManagementInterface = NULL;
static QDBusInterface* consoleKitInterface = NULL;

static bool setupPowerManagementInterface ()
{
    if (!powerManagementInterface)
        powerManagementInterface = new QDBusInterface("org.freedesktop.PowerManagement","/org/freedesktop/PowerManagement", "org.freedesktop.PowerManagement");
    return powerManagementInterface != NULL;
}

static bool setupConsoleKitInterface ()
{
    if (!consoleKitInterface)
        consoleKitInterface = new QDBusInterface("org.freedesktop.ConsoleKit", "/org/freedesktop/ConsoleKit/Manager", "org.freedesktop.ConsoleKit");
    return consoleKitInterface != NULL;
}

bool canSuspend()
{
    if (!setupPowerManagementInterface())
        return false;

    QDBusReply<bool> reply = powerManagementInterface->call("CanSuspend");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void suspend()
{
    if (setupPowerManagementInterface())
        powerManagementInterface->call("Suspend");
}

bool canHibernate()
{
    if (!setupPowerManagementInterface())
        return false;

    QDBusReply<bool> reply = powerManagementInterface->call("CanHibernate");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void hibernate()
{
    if (setupConsoleKitInterface())
        powerManagementInterface->call("Hibernate");
}

bool canShutdown()
{
    if (!setupConsoleKitInterface())
        return false;

    QDBusReply<bool> reply = consoleKitInterface->call("CanStop");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void shutdown()
{
    if (setupConsoleKitInterface())
        consoleKitInterface->call("stop");
}

bool canRestart()
{
    if (!setupConsoleKitInterface())
        return false;

    QDBusReply<bool> reply = consoleKitInterface->call("CanRestart");
    if (reply.isValid())
        return reply.value();
    else
        return false;
}

void restart()
{
    if (setupConsoleKitInterface())
        consoleKitInterface->call("Restart");
}
