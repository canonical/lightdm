#include "config.h"

#include <QtCore/QSettings>

using namespace QLightDM;

class ConfigPrivate {
public:
    QSettings* settings;
};

Config::Config(QString filePath, QObject *parent) :
    QObject(parent),
    d (new ConfigPrivate())
{
    d->settings = new QSettings(filePath, QSettings::IniFormat, this);
}

Config::~Config()
{
    delete d;
}


int Config::minimumUid() const
{
    return d->settings->value("UserManager/minimum-uid", QVariant(500)).toInt();
}

QStringList Config::hiddenShells() const
{
    if (d->settings->contains("UserManager/hidden-shells")) {
        return d->settings->value("UserManager/hidden-shells").toString().split(" ");
    } else {
        return QStringList() << "/bin/false" << "/usr/sbin/nologin";
    }
}

QStringList Config::hiddenUsers() const
{
    if (d->settings->contains("UserManager/hidden-shells")) {
        return d->settings->value("UserManager/hidden-shells").toString().split(" ");
    } else {
        return QStringList() << "nobody" << "nobody4" << "noaccess";
    }
}

bool QLightDM::Config::loadUsers() const
{
    return d->settings->value("UserManager/load-users", QVariant(true)).toBool();
}
