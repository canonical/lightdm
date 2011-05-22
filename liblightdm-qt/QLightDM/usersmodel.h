#ifndef USERSMODEL_H
#define USERSMODEL_H

#include <QAbstractListModel>

class UsersModelPrivate;

namespace QLightDM
{

class Config;

class Q_DECL_EXPORT UsersModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit UsersModel(QLightDM::Config *config, QObject *parent = 0);
    ~UsersModel();
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;

signals:

public slots:

private slots:
    void loadUsers();

private:
    UsersModelPrivate *d;
};
};

#endif // USERSMODEL_H
