#ifndef LDMSESSIONSMODEL_H
#define LDMSESSIONSMODEL_H

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

#endif // LDMSESSIONSMODEL_H
