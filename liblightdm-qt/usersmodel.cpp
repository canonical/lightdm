/*
 * Copyright (C) 2010-2011 David Edmundson.
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 or version 3 of the License.
 * See http://www.gnu.org/copyleft/lgpl.html the full text of the license.
 */

#include "QLightDM/usersmodel.h"

#include <QtCore/QString>
#include <QtCore/QDebug>
#include <QtGui/QIcon>

#include <lightdm.h>

using namespace QLightDM;

class UserItem
{
public:
    QString name;
    QString realName;
    QString homeDirectory;
    QString image;
    QString background;
    QString session;
    bool isLoggedIn;
    bool hasMessages;
    quint64 uid;
    QString displayName() const;
    bool isLocked;
};

QString UserItem::displayName() const {
    if (realName.isEmpty()){
        return name;
    }
    else {
        return realName;
    }
}

namespace QLightDM {
class UsersModelPrivate {
public:
    UsersModelPrivate(UsersModel *parent);
    virtual ~UsersModelPrivate();
    QList<UserItem> users;

    protected:
        UsersModel * const q_ptr;

        void loadUsers();

        static void cb_userAdded(LightDMUserList *user_list, LightDMUser *user, gpointer data);
        static void cb_userChanged(LightDMUserList *user_list, LightDMUser *user, gpointer data);
        static void cb_userRemoved(LightDMUserList *user_list, LightDMUser *user, gpointer data);
    private:
        Q_DECLARE_PUBLIC(UsersModel)
};
}

UsersModelPrivate::UsersModelPrivate(UsersModel* parent) :
    q_ptr(parent)
{
#if !defined(GLIB_VERSION_2_36)
    g_type_init();
#endif
}

UsersModelPrivate::~UsersModelPrivate()
{
    g_signal_handlers_disconnect_by_data(lightdm_user_list_get_instance(), this);
}

void UsersModelPrivate::loadUsers()
{
    Q_Q(UsersModel);

    int rowCount = lightdm_user_list_get_length(lightdm_user_list_get_instance());

    if (rowCount == 0) {
        return;
    } else {
        q->beginInsertRows(QModelIndex(), 0, rowCount-1);

        const GList *items, *item;
        items = lightdm_user_list_get_users(lightdm_user_list_get_instance());
        for (item = items; item; item = item->next) {
            LightDMUser *ldmUser = static_cast<LightDMUser*>(item->data);

            UserItem user;
            user.name = QString::fromUtf8(lightdm_user_get_name(ldmUser));
            user.homeDirectory = QString::fromUtf8(lightdm_user_get_home_directory(ldmUser));
            user.realName = QString::fromUtf8(lightdm_user_get_real_name(ldmUser));
            user.image = QString::fromUtf8(lightdm_user_get_image(ldmUser));
            user.background = QString::fromUtf8(lightdm_user_get_background(ldmUser));
            user.session = QString::fromUtf8(lightdm_user_get_session(ldmUser));
            user.isLoggedIn = lightdm_user_get_logged_in(ldmUser);
            user.hasMessages = lightdm_user_get_has_messages(ldmUser);
            user.uid = (quint64)lightdm_user_get_uid(ldmUser);
            user.isLocked = lightdm_user_get_is_locked(ldmUser);
            users.append(user);
        }

        q->endInsertRows();
    }
    g_signal_connect(lightdm_user_list_get_instance(), LIGHTDM_USER_LIST_SIGNAL_USER_ADDED, G_CALLBACK (cb_userAdded), this);
    g_signal_connect(lightdm_user_list_get_instance(), LIGHTDM_USER_LIST_SIGNAL_USER_CHANGED, G_CALLBACK (cb_userChanged), this);
    g_signal_connect(lightdm_user_list_get_instance(), LIGHTDM_USER_LIST_SIGNAL_USER_REMOVED, G_CALLBACK (cb_userRemoved), this);
}

void UsersModelPrivate::cb_userAdded(LightDMUserList *user_list, LightDMUser *ldmUser, gpointer data)
{
    Q_UNUSED(user_list)
    UsersModelPrivate *that = static_cast<UsersModelPrivate*>(data);

    that->q_func()->beginInsertRows(QModelIndex(), that->users.size(), that->users.size());

    UserItem user;
    user.name = QString::fromUtf8(lightdm_user_get_name(ldmUser));
    user.homeDirectory = QString::fromUtf8(lightdm_user_get_home_directory(ldmUser));
    user.realName = QString::fromUtf8(lightdm_user_get_real_name(ldmUser));
    user.image = QString::fromUtf8(lightdm_user_get_image(ldmUser));
    user.background = QString::fromUtf8(lightdm_user_get_background(ldmUser));
    user.isLoggedIn = lightdm_user_get_logged_in(ldmUser);
    user.hasMessages = lightdm_user_get_has_messages(ldmUser);
    user.uid = (quint64)lightdm_user_get_uid(ldmUser);
    user.isLocked = lightdm_user_get_is_locked(ldmUser);
    that->users.append(user);

    that->q_func()->endInsertRows();

}

void UsersModelPrivate::cb_userChanged(LightDMUserList *user_list, LightDMUser *ldmUser, gpointer data)
{
    Q_UNUSED(user_list)
    UsersModelPrivate *that = static_cast<UsersModelPrivate*>(data);

    QString userToChange = QString::fromUtf8(lightdm_user_get_name(ldmUser));

    for (int i=0;i<that->users.size();i++) {
        if (that->users[i].name == userToChange) {

            that->users[i].homeDirectory = QString::fromUtf8(lightdm_user_get_home_directory(ldmUser));
            that->users[i].realName = QString::fromUtf8(lightdm_user_get_real_name(ldmUser));
            that->users[i].image = QString::fromUtf8(lightdm_user_get_image(ldmUser));
            that->users[i].background = QString::fromUtf8(lightdm_user_get_background(ldmUser));
            that->users[i].isLoggedIn = lightdm_user_get_logged_in(ldmUser);
            that->users[i].hasMessages = lightdm_user_get_has_messages(ldmUser);
            that->users[i].uid = (quint64)lightdm_user_get_uid(ldmUser);
            that->users[i].isLocked = lightdm_user_get_is_locked(ldmUser);

            QModelIndex index = that->q_ptr->createIndex(i, 0);
            that->q_ptr->dataChanged(index, index);
            break;
        }
    }
}


void UsersModelPrivate::cb_userRemoved(LightDMUserList *user_list, LightDMUser *ldmUser, gpointer data)
{
    Q_UNUSED(user_list)

    UsersModelPrivate *that = static_cast<UsersModelPrivate*>(data);
    QString userToRemove = QString::fromUtf8(lightdm_user_get_name(ldmUser));

    for (int i=0;i<that->users.size();i++) {
        if (that->users[i].name == userToRemove) {
            that->q_ptr->beginRemoveRows(QModelIndex(), i, i);
            that->users.removeAt(i);
            that->q_ptr->endRemoveRows();
            break;
        }
    }
}

UsersModel::UsersModel(QObject *parent) :
    QAbstractListModel(parent),
    d_ptr(new UsersModelPrivate(this))
{
    Q_D(UsersModel);
    // Extend roleNames (we want to keep the "display" role)
    QHash<int, QByteArray> roles = roleNames();
    roles[NameRole] = "name";
    roles[RealNameRole] = "realName";
    roles[LoggedInRole] = "loggedIn";
    roles[BackgroundRole] = "background";
    roles[BackgroundPathRole] = "backgroundPath";
    roles[SessionRole] = "session";
    roles[HasMessagesRole] = "hasMessages";
    roles[ImagePathRole] = "imagePath";
    roles[UidRole] = "uid";
    roles[IsLockedRole] = "isLocked";
    setRoleNames(roles);
    d->loadUsers();

}

UsersModel::~UsersModel()
{
    delete d_ptr;
}


int UsersModel::rowCount(const QModelIndex &parent) const
{
    Q_D(const UsersModel);
    if (parent == QModelIndex()) {
        return d->users.size();
    }

    return 0;
}

QVariant UsersModel::data(const QModelIndex &index, int role) const
{
    Q_D(const UsersModel);

    if (!index.isValid()) {
        return QVariant();
    }

    int row = index.row();
    switch (role) {
    case Qt::DisplayRole:
        return d->users[row].displayName();
    case Qt::DecorationRole:
        return QIcon(d->users[row].image);
    case UsersModel::NameRole:
        return d->users[row].name;
    case UsersModel::RealNameRole:
        return d->users[row].realName;
    case UsersModel::SessionRole:
        return d->users[row].session;
    case UsersModel::LoggedInRole:
        return d->users[row].isLoggedIn;
    case UsersModel::BackgroundRole:
        return QPixmap(d->users[row].background);
    case UsersModel::BackgroundPathRole:
        return d->users[row].background;
    case UsersModel::HasMessagesRole:
        return d->users[row].hasMessages;
    case UsersModel::ImagePathRole:
        return d->users[row].image;
    case UsersModel::UidRole:
        return d->users[row].uid;
    case UsersModel::IsLockedRole:
        return d->users[row].isLocked;
    }

    return QVariant();
}


#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include "usersmodel_moc5.cpp"
#else
#include "usersmodel_moc4.cpp"
#endif
