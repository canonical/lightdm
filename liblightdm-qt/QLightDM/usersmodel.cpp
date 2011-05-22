#include "usersmodel.h"
#include "user.h"
#include "config.h"

#include <pwd.h>
#include <errno.h>

#include <QtCore/QString>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QDebug>

#include <QtGui/QPixmap>

using namespace QLightDM;

class UsersModelPrivate {
public:
    QList<User> users;
    QLightDM::Config *config;
};

UsersModel::UsersModel(QLightDM::Config *config, QObject *parent) :
    QAbstractListModel(parent),
    d (new UsersModelPrivate())
{
    d->config = config;

    if (d->config->loadUsers()) {
        //load users on startup and if the password file changes.
        QFileSystemWatcher *watcher = new QFileSystemWatcher(this);
        watcher->addPath("/etc/passwd"); //FIXME harcoded path
        connect(watcher, SIGNAL(fileChanged(QString)), SLOT(loadUsers()));

        loadUsers();
    }
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
    }

    return QVariant();
}


void UsersModel::loadUsers()
{
    QStringList hiddenUsers, hiddenShells;
    int minimumUid;
    QList<User> newUsers;

    minimumUid = d->config->minimumUid();
    hiddenUsers = d->config->hiddenUsers();
    hiddenShells = d->config->hiddenShells();
    //FIXME accidently not got the "if contact removed" code. Need to fix.

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
        if(!entry)
            break;

        /* Ignore system users */
        if(entry->pw_uid < minimumUid)
            continue;

        /* Ignore users disabled by shell */
        if(entry->pw_shell)
        {
            for(i = 0; i < hiddenShells.size(); i++)
                if(entry->pw_shell == hiddenShells.at(i))
                    break;
            if(i < hiddenShells.size())
                continue;
        }

        /* Ignore certain users */
        for(i = 0; i < hiddenUsers.size(); i++)
            if(entry->pw_name == hiddenUsers.at(i))
                break;
        if(i < hiddenUsers.size())
            continue;

        tokens = QString(entry->pw_gecos).split(",");
        if(tokens.size() > 0 && tokens.at(i) != "")
            realName = tokens.at(i);


        //replace this with QFile::exists();
        QDir homeDir(entry->pw_dir);
        imageFile = new QFile(homeDir.filePath(".face"));
        if(!imageFile->exists())
        {
            delete imageFile;
            imageFile = new QFile(homeDir.filePath(".face.icon"));
        }
        if(imageFile->exists()) {
            image = "file://" + imageFile->fileName();
        }
        delete imageFile;

        //FIXME don't create objects on the heap in the middle of a loop with breaks in it! Destined for fail.
        //FIXME pointers all over the place in this code.
        User user(entry->pw_name, realName, entry->pw_dir, image, false);

        /* Update existing users if have them */
        bool matchedUser = false;

        for (int i=0; i < d->users.size(); i++)
        {
            if(d->users[i].name() == user.name()) {
                matchedUser = true;
                d->users[i].update(user.realName(), user.homeDirectory(), user.image(), user.isLoggedIn());
                dataChanged(createIndex(i, 0), createIndex(i,0));
            }
        }
        if(!matchedUser) {
            newUsers.append(user);
        }
    }

    if(errno != 0) {
        qDebug() << "Failed to read password database: " << strerror(errno);
    }

    endpwent();

    //FIXME accidently not got the "if contact removed" code. Need to restore that.
    //should call beginRemoveRows, and then remove the row from the model.
    //might get rid of "User" object, keep as private object (like sessionsmodel) - or make it copyable.


    //append new users
    if (newUsers.size() > 0) {
        beginInsertRows(QModelIndex(), 0, newUsers.size());
        d->users.append(newUsers);
        endInsertRows();
    }
}
