#include "ldmsessionsmodel.h"

#include <QtCore/QList>
#include <QtCore/QDir>
#include <QtCore/QVariant>
#include <QtCore/QSettings>

class SessionItem;

class LdmSessionsModelPrivate
{
public:
    QList<SessionItem> items;
};

class SessionItem
{
public:
    //FIXME can I make these consts, if I set them in a lovely constructor?
    QString id;
    QString name;
    QString comment;
};

LdmSessionsModel::LdmSessionsModel(QObject *parent) :
    QAbstractListModel(parent),
    d(new LdmSessionsModelPrivate())
{
    buildList();
}

LdmSessionsModel::~LdmSessionsModel()
{
}

int LdmSessionsModel::rowCount(const QModelIndex &parent) const
{
    if (parent == QModelIndex()) { //if top level
        return d->items.size();
    } else {
        return 0; // no child elements.
    }
}

QVariant LdmSessionsModel::data(const QModelIndex &index, int role) const
{
    if (! index.isValid()) {
        return QVariant();
    }

    int row = index.row();

    switch (role) {
    case Qt::DisplayRole:
        return d->items[row].name;
    case Qt::ToolTipRole:
        return d->items[row].comment;

    }
    return QVariant();
}

void LdmSessionsModel::buildList()
{
    //maybe clear first?

    //FIXME don't hardcode this!
    QDir sessionDir("/usr/share/xsessions");
    sessionDir.setNameFilters(QStringList() << "*.desktop");

    QList<SessionItem> items;
    
    foreach(QString sessionFileName, sessionDir.entryList()) {
        QSettings sessionData(sessionDir.filePath(sessionFileName), QSettings::IniFormat);
        sessionData.beginGroup("Desktop Entry");
        sessionFileName.chop(8);// chop(8) removes '.desktop'

        SessionItem item;
        item.id = sessionFileName;
        item.name  = sessionData.value("Name").toString();
        item.comment = sessionData.value("Comment").toString();
        items.append(item);
    }

    beginInsertRows(QModelIndex(), 0, items.size());
    d->items.append(items);
    endInsertRows();
}

