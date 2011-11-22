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

#include "QLightDM/sessionsmodel.h"

#include <QtCore/QVariant>
#include <QtCore/QDebug>

#include <lightdm-gobject-1/lightdm.h>

using namespace QLightDM;

class SessionItem
{
public:
    QString id;
    QString name;
    QString comment;
};


class SessionsModelPrivate
{
public:
    SessionsModelPrivate(SessionsModel *parent);
    QList<SessionItem> items;
    
    void loadSessions();
    
protected:
    SessionsModel* q_ptr;

private:
    Q_DECLARE_PUBLIC(SessionsModel)
    
};

SessionsModelPrivate::SessionsModelPrivate(SessionsModel *parent) :
    q_ptr(parent)
{
    g_type_init();
    loadSessions();
}

void SessionsModelPrivate::loadSessions()
{
    qDebug() << "loading sessions";

   GList *ldmSessions = lightdm_get_sessions();
   for (GList* item = ldmSessions; item; item = item->next) {
       LightDMSession *ldmSession = static_cast<LightDMSession*>(item->data);
       Q_ASSERT(ldmSession);

       SessionItem session;
       session.id = QString::fromLocal8Bit(lightdm_session_get_key(ldmSession));
       session.name = QString::fromLocal8Bit(lightdm_session_get_name(ldmSession));
       session.comment = QString::fromLocal8Bit(lightdm_session_get_comment(ldmSession));

       qDebug() << "adding session" << session.id;

       items.append(session);
   }

   //this happens in the constructor so we don't need beginInsertRows() etc.
}


SessionsModel::SessionsModel(QObject *parent) :
    QAbstractListModel(parent),
    d_ptr(new SessionsModelPrivate(this))
{
}

SessionsModel::~SessionsModel()
{
    delete d_ptr;
}

int SessionsModel::rowCount(const QModelIndex &parent) const
{
    Q_D(const SessionsModel);
    
    if (parent == QModelIndex()) { //if top level
        return d->items.size();
    } else {
        return 0; // no child elements.
    }
}

QVariant SessionsModel::data(const QModelIndex &index, int role) const
{
    Q_D(const SessionsModel);

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

#include "sessionsmodel_moc.cpp"
