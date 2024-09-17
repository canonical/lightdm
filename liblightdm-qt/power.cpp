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

#include <lightdm.h>

using namespace QLightDM;

class PowerInterface::PowerInterfacePrivate
{
public:
    PowerInterfacePrivate();
};

PowerInterface::PowerInterfacePrivate::PowerInterfacePrivate()
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
    return lightdm_get_can_suspend ();
}

bool PowerInterface::suspend()
{
    return lightdm_suspend (NULL);
}

bool PowerInterface::canHibernate()
{
    return lightdm_get_can_hibernate ();
}

bool PowerInterface::hibernate()
{
    return lightdm_hibernate (NULL);  
}

bool PowerInterface::canShutdown()
{
    return lightdm_get_can_shutdown ();
}

bool PowerInterface::shutdown()
{
    return lightdm_shutdown (NULL);
}

bool PowerInterface::canRestart()
{
    return lightdm_get_can_restart ();
}

bool PowerInterface::restart()
{
    return lightdm_restart (NULL);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include "power_moc6.cpp"
#else
#include "power_moc5.cpp"
#endif
