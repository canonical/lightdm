/*
 * Copyright (C) 2010-2011 David Edmundson.
 * Copyright (C) 2010-2011 Robert Ancell
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef QLIGHTDM_POWER_H
#define QLIGHTDM_POWER_H

#include <QObject>

namespace QLightDM
{
    class Q_DECL_EXPORT PowerInterface : public QObject
    {
        Q_OBJECT
    public:
        Q_PROPERTY(bool canSuspend READ canSuspend() CONSTANT)
        Q_PROPERTY(bool canHibernate READ canHibernate() CONSTANT)
        Q_PROPERTY(bool canShutdown READ canShutdown() CONSTANT)
        Q_PROPERTY(bool canRestart READ canRestart() CONSTANT)

        PowerInterface(QObject *parent=0);
        virtual ~PowerInterface();

        bool canSuspend();
        bool canHibernate();
        bool canShutdown();
        bool canRestart();

    public Q_SLOTS:
        bool suspend();
        bool hibernate();
        bool shutdown();
        bool restart();

    private:
        class PowerInterfacePrivate;
        PowerInterfacePrivate * const d;

    };
}

#endif // QLIGHTDM_POWER_H
