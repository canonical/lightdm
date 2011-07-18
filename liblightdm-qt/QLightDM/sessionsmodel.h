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

#ifndef QLIGTHDM_SESSIONSMODEL_H
#define QLIGTHDM_SESSIONSMODEL_H

#include <QtCore/QAbstractListModel>

class SessionsModelPrivate;

namespace QLightDM {
    class Q_DECL_EXPORT SessionsModel : public QAbstractListModel
    {
        Q_OBJECT
    public:
        enum SessionModelRoles {IdRole = Qt::UserRole};

        explicit SessionsModel(QObject *parent = 0);
        virtual ~SessionsModel();

        int rowCount(const QModelIndex &parent) const;
        QVariant data(const QModelIndex &index, int role=Qt::DisplayRole) const;

    private:
        SessionsModelPrivate *d;
        void buildList(); //maybe make this a public slot, which apps can call only if they give a care about the session.
    };
};

#endif // QLIGHTDM_SESSIONSMODEL_H
