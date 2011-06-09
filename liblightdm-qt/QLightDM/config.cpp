/*
 * Copyright (C) 2010-2011 David Edmundson.
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 * 
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#include "config.h"

#include <QtCore/QSettings>

#include <QDebug>

using namespace QLightDM;

class ConfigPrivate {
public:
    QSettings* settings;
};

Config::Config(QString filePath, QObject *parent) :
    QObject(parent),
    d (new ConfigPrivate())
{
    qDebug() << "creating config";
    qDebug() << this;
    d->settings = new QSettings(filePath, QSettings::IniFormat, this);
    qDebug() << d->settings;
    qDebug() << d->settings->value("UserManager/load-users", QVariant(true)).toBool();
}

Config::~Config()
{
    qDebug() << "deleting config";

    delete d;
}


int Config::minimumUid() const
{
    return d->settings->value("UserManager/minimum-uid", QVariant(500)).toInt();
}

QStringList Config::hiddenShells() const
{
    if (d->settings->contains("UserManager/hidden-shells")) {
        return d->settings->value("UserManager/hidden-shells").toString().split(" ");
    } else {
        return QStringList() << "/bin/false" << "/usr/sbin/nologin";
    }
}

QStringList Config::hiddenUsers() const
{
    if (d->settings->contains("UserManager/hidden-shells")) {
        return d->settings->value("UserManager/hidden-shells").toString().split(" ");
    } else {
        return QStringList() << "nobody" << "nobody4" << "noaccess";
    }
}

bool QLightDM::Config::loadUsers() const
{
    qDebug() << this;
    qDebug() << d->settings;
    return d->settings->value("UserManager/load-users", QVariant(true)).toBool();
}
