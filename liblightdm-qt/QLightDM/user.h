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

#ifndef QLIGHTDM_USER_H
#define QLIGHTDM_USER_H

#include <QtCore/QString>
#include <QtCore/QSharedDataPointer>
#include <QAbstractListModel>

class UserPrivate;
class UsersModelPrivate;
class UserItem;

namespace QLightDM
{
class Q_DECL_EXPORT UsersModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit UsersModel(QObject *parent = 0);
    ~UsersModel();

    enum UserModelRoles {NameRole = Qt::UserRole,
                         RealNameRole,
                         LoggedInRole};

    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;

private slots:
    /** Updates the model with new changes in the password file*/
    void loadUsers();

private:
    UsersModelPrivate *d;
    QList<UserItem> getUsers() const;
};

};

#endif // QLIGHTDM_USER_H
