/*
 * Copyright (C) 2010-2011 David Edmundson.
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include "QLightDM/sessionsmodel.h"

#include <QtCore/QVariant>
#include <QtCore/QDebug>

#include <lightdm.h>

using namespace QLightDM;

class SessionItem
{
public:
    QString key;
    QString type;
    QString name;
    QString comment;
};


class SessionsModelPrivate
{
public:
    SessionsModelPrivate(SessionsModel *parent);
    QList<SessionItem> items;

    void loadSessions(SessionsModel::SessionType sessionType);

protected:
    SessionsModel* q_ptr;

private:
    Q_DECLARE_PUBLIC(SessionsModel)

};

SessionsModelPrivate::SessionsModelPrivate(SessionsModel *parent) :
    q_ptr(parent)
{
#if !defined(GLIB_VERSION_2_36)
    g_type_init();
#endif
}

void SessionsModelPrivate::loadSessions(SessionsModel::SessionType sessionType)
{
    GList *ldmSessions;

    switch (sessionType) {
    case SessionsModel::RemoteSessions:
        ldmSessions = lightdm_get_remote_sessions();
        break;
    case SessionsModel::LocalSessions:
        /* Fall through*/
    default:
        ldmSessions = lightdm_get_sessions();
        break;
    }

    for (GList* item = ldmSessions; item; item = item->next) {
       LightDMSession *ldmSession = static_cast<LightDMSession*>(item->data);
       Q_ASSERT(ldmSession);

       SessionItem session;
       session.key = QString::fromUtf8(lightdm_session_get_key(ldmSession));
       session.type = QString::fromUtf8(lightdm_session_get_session_type(ldmSession));
       session.name = QString::fromUtf8(lightdm_session_get_name(ldmSession));
       session.comment = QString::fromUtf8(lightdm_session_get_comment(ldmSession));

       items.append(session);
   }

   //this happens in the constructor so we don't need beginInsertRows() etc.
}


//deprecated constructor for ABI compatibility.
SessionsModel::SessionsModel(QObject *parent) :
    QAbstractListModel(parent),
    d_ptr(new SessionsModelPrivate(this))
{
    Q_D(SessionsModel);

    d->loadSessions(SessionsModel::LocalSessions);
}

SessionsModel::SessionsModel(SessionsModel::SessionType sessionType, QObject *parent) :
    QAbstractListModel(parent),
    d_ptr(new SessionsModelPrivate(this))
{
    Q_D(SessionsModel);

    d->loadSessions(sessionType);
}

SessionsModel::~SessionsModel()
{
    delete d_ptr;
}

QHash<int, QByteArray> SessionsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[KeyRole] = "key";
    roles[Qt::DisplayRole] = "display";
    roles[Qt::ToolTipRole] = "toolTip";
    return roles;
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
    case SessionsModel::KeyRole:
        return d->items[row].key;
    case SessionsModel::TypeRole:
        return d->items[row].type;
    case Qt::DisplayRole:
        return d->items[row].name;
    case Qt::ToolTipRole:
        return d->items[row].comment;

    }
    return QVariant();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include "sessionsmodel_moc6.cpp"
#else
#include "sessionsmodel_moc5.cpp"
#endif
