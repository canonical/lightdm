#ifndef USERSMODEL_H
#define USERSMODEL_H

#include <QAbstractListModel>

class UsersModelPrivate;

namespace QLightDM
{

class Config;
class User;

class Q_DECL_EXPORT UsersModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum UserModelRoles {NameRole = Qt::UserRole,
                         RealNameRole,
                         LoggedInRole
                        };

    explicit UsersModel(QLightDM::Config *config, QObject *parent = 0);
    ~UsersModel();
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;

signals:

public slots:

private slots:
    /** Updates the model with new changes in the password file*/
    void loadUsers();

private:
    /** Returns a list of all users in the password file*/
    QList<User> getUsers();
    UsersModelPrivate *d;
};
};

#endif // USERSMODEL_H
