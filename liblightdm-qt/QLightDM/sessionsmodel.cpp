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

#include "sessionsmodel.h"

#include <QtCore/QList>
#include <QtCore/QDir>
#include <QtCore/QVariant>
#include <QtCore/QSettings>

using namespace QLightDM;

class SessionItem;

class SessionsModelPrivate
{
public:
    QList<SessionItem> items;
};

class SessionItem
{
public:
    //FIXME can I make these consts, if I set them in a lovely constructor?
    QString id;
    QString name;
    QString comment;
};

SessionsModel::SessionsModel(QObject *parent) :
    QAbstractListModel(parent),
    d(new SessionsModelPrivate())
{
    buildList();
}

SessionsModel::~SessionsModel()
{
}

int SessionsModel::rowCount(const QModelIndex &parent) const
{
    if (parent == QModelIndex()) { //if top level
        return d->items.size();
    } else {
        return 0; // no child elements.
    }
}

QVariant SessionsModel::data(const QModelIndex &index, int role) const
{
    if (! index.isValid()) {
        return QVariant();
    }

    int row = index.row();

    switch (role) {
    case SessionsModel::IdRole:
        return d->items[row].id;
    case Qt::DisplayRole:
        return d->items[row].name;
    case Qt::ToolTipRole:
        return d->items[row].comment;

    }
    return QVariant();
}

void SessionsModel::buildList()
{
    //maybe clear first?

    //FIXME don't hardcode this!
    QDir sessionDir("/usr/share/xsessions");
    sessionDir.setNameFilters(QStringList() << "*.desktop");

    QList<SessionItem> items;
    
    foreach(QString sessionFileName, sessionDir.entryList()) {
        QSettings sessionData(sessionDir.filePath(sessionFileName), QSettings::IniFormat);
        sessionData.beginGroup("Desktop Entry");
        sessionFileName.chop(8);// chop(8) removes '.desktop'

        SessionItem item;
        item.id = sessionFileName;
        item.name  = sessionData.value("Name").toString();
        item.comment = sessionData.value("Comment").toString();
        items.append(item);
    }

    beginInsertRows(QModelIndex(), 0, items.size());
    d->items.append(items);
    endInsertRows();
}

