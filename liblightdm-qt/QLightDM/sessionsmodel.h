/*
 * Copyright (C) 2010-2011 David Edmundson.
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#ifndef QLIGHTDM_SESSIONS_MODEL_H
#define QLIGHTDM_SESSIONS_MODEL_H

#include <QtCore/QAbstractListModel>

class SessionsModelPrivate;

namespace QLightDM {
    class Q_DECL_EXPORT SessionsModel : public QAbstractListModel
    {
        Q_OBJECT

        Q_ENUMS(SessionModelRoles SessionType)

    public:
        enum SessionModelRoles {
            //name is exposed as Qt::DisplayRole
            //comment is exposed as Qt::TooltipRole
            KeyRole = Qt::UserRole,
            IdRole = KeyRole, /** Deprecated */
            TypeRole
        };

        enum SessionType {
            LocalSessions,
            RemoteSessions
        };

        explicit SessionsModel(QObject *parent = 0); /** Deprecated. Loads local sessions*/
        explicit SessionsModel(SessionsModel::SessionType, QObject *parent = 0);
        virtual ~SessionsModel();

        QHash<int, QByteArray> roleNames() const;
        int rowCount(const QModelIndex &parent) const;
        QVariant data(const QModelIndex &index, int role=Qt::DisplayRole) const;

    protected:
        SessionsModelPrivate *d_ptr;

    private:
        Q_DECLARE_PRIVATE(SessionsModel)
    };
}

#endif // QLIGHTDM_SESSION_H
