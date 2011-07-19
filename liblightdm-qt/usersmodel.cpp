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

#include "QLightDM/UsersModel"

#include <pwd.h>
#include <errno.h>

#include <QtCore/QString>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtCore/QDebug>

#include <QtGui/QPixmap>

using namespace QLightDM;

class UsersModelPrivate {
public:
    QList<User> users;
};

UsersModel::UsersModel(QObject *parent) :
    QAbstractListModel(parent),
    d (new UsersModelPrivate())
{
    //load users on startup and if the password file changes.
    QFileSystemWatcher *watcher = new QFileSystemWatcher(this);
    watcher->addPath("/etc/passwd"); //FIXME harcoded path
    connect(watcher, SIGNAL(fileChanged(QString)), SLOT(loadUsers()));

    loadUsers();
}

UsersModel::~UsersModel()
{
    delete d;
}


int UsersModel::rowCount(const QModelIndex &parent) const
{
    return d->users.count();
}

QVariant UsersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    int row = index.row();
    switch (role) {
    case Qt::DisplayRole:
        return d->users[row].displayName();
    case Qt::DecorationRole:
        return QPixmap(d->users[row].image());
    case UsersModel::NameRole:
        return d->users[row].name();
    case UsersModel::RealNameRole:
        return d->users[row].realName();
    case UsersModel::LoggedInRole:
        return d->users[row].isLoggedIn();
    }

    return QVariant();
}


QList<User> UsersModel::getUsers()
{
    QString file = "/etc/lightdm/users.conf";
    qDebug() << "Loading user configuration from " << file;
    QSettings *settings = new QSettings(file, QSettings::IniFormat);

    int minimumUid = settings->value("UserAccounts/minimum-uid", QVariant(500)).toInt();
    QStringList hiddenShells;
    if (settings->contains("UserAccounts/hidden-shells"))
        hiddenShells = settings->value("UserAccounts/hidden-shells").toString().split(" ");
    else
        hiddenShells = QStringList() << "/bin/false" << "/usr/sbin/nologin";
    QStringList hiddenUsers;
    if (settings->contains("UserAccounts/hidden-users"))
        hiddenUsers = settings->value("UserAccounts/hidden-users").toString().split(" ");
    else
        hiddenUsers = QStringList() << "nobody" << "nobody4" << "noaccess";

    QList<User> users;

    setpwent();
    while(TRUE)
    {
        struct passwd *entry;
        QStringList tokens;
        QString realName, image;
        QFile *imageFile;
        int i;

        errno = 0;
        entry = getpwent();
        if(!entry) {
            break;
        }

        /* Ignore system users */
        if(entry->pw_uid < minimumUid) {
            continue;
        }

        /* Ignore users disabled by shell */
        if(entry->pw_shell) {
            if (hiddenShells.contains(entry->pw_shell)) {
                continue;
            }
        }

        if (hiddenUsers.contains(entry->pw_name)) {
            continue;
        }

        tokens = QString(entry->pw_gecos).split(",");
        if(tokens.size() > 0 && tokens.at(i) != "")
            realName = tokens.at(i);

        //replace this with QFile::exists();
        QDir homeDir(entry->pw_dir);
        imageFile = new QFile(homeDir.filePath(".face"));
        if(!imageFile->exists()) {
            delete imageFile;
            imageFile = new QFile(homeDir.filePath(".face.icon"));
        }
        if(imageFile->exists())
        {
            delete imageFile;
            imageFile = NULL;
        }

        User user(entry->pw_name, realName, entry->pw_dir, imageFile->fileName(), false);
        users.append(user);
    }

    if(errno != 0) {
        qDebug() << "Failed to read password database: " << strerror(errno);
    }
    endpwent();

    delete settings;

    return users;
}

void UsersModel::loadUsers()
{
    QList<User> usersToAdd;

    //FIXME accidently not got the "if contact removed" code. Need to restore that.
    //should call beginRemoveRows, and then remove the row from the model.
    //might get rid of "User" object, keep as private object (like sessionsmodel) - or make it copyable.

    //loop through all the new list of users, if it's in the list already update it (or do nothing) otherwise append to list of new users
    QList<User> newUserList = getUsers();

    foreach(const User &user, newUserList) {
        bool alreadyInList = false;
        for(int i=0; i < d->users.size(); i++) {
            if (user.name() == d->users[i].name()) {
                alreadyInList = true;
                d->users[i].update(user.name(), user.homeDirectory(), user.image(), user.isLoggedIn());
                QModelIndex index = createIndex(i,0);
                dataChanged(index, index);
            }
        }

        if (!alreadyInList) {
            usersToAdd.append(user);
        }
    }

    //loop through all the existing users, if they're not in the newly gathered list, remove them.

    //FIXME this isn't perfect, looping like this in a mutating list - use mutable iterator.
    for (int i=0; i < d->users.size() ; i++) {
        bool found = false;
        foreach(const User &user, newUserList) {
            if (d->users[i].name() == user.name()) {
                found = true;
            }
        }

        if (!found) {
            beginRemoveRows(QModelIndex(), i, i);
            d->users.removeAt(i);
            endRemoveRows();
        }
    }

    //append new users
    if (usersToAdd.size() > 0) {
        beginInsertRows(QModelIndex(), d->users.size(), d->users.size() + usersToAdd.size() -1);
        d->users.append(usersToAdd);
        endInsertRows();
    }
}
