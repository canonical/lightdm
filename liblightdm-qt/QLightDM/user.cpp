#include "user.h"

#include <QtCore/QSharedData>

using namespace QLightDM;

class UserPrivate : public QSharedData
{
public:
    QString name;
    QString realName;
    QString homeDirectory;
    QString image;
    bool isLoggedIn;
};

User::User():
    d(new UserPrivate)
{
}

User::User(const QString& name, const QString& realName, const QString& homeDirectory, const QString& image, bool isLoggedIn) :
    d(new UserPrivate)
{
    d->name = name;
    d->realName = realName;
    d->homeDirectory = homeDirectory;
    d->image = image;
    d->isLoggedIn = isLoggedIn;
}

User::User(const User &other)
    : d(other.d)
{
}

User::~User()
{
}


User& User::operator=(const User& other)
{
    d = other.d;
    return *this;
}

bool User::update(const QString& realName, const QString& homeDirectory, const QString& image, bool isLoggedIn)
{
    if (d->realName == realName && d->homeDirectory == homeDirectory && d->image == image && d->isLoggedIn == isLoggedIn) {
        return false;
    }

    d->realName = realName;
    d->homeDirectory = homeDirectory;
    d->image = image;
    d->isLoggedIn = isLoggedIn;

    return true;
}

QString User::displayName() const
{
    if (!d->realName.isEmpty()) {
        return d->realName;
    }
    else {
        return d->name;
    }
}

QString User::name() const
{
    return d->name;
}

QString User::realName() const
{
    return d->realName;
}

QString User::homeDirectory() const
{
    return d->homeDirectory;
}

QString User::image() const
{
    return d->image;
}

bool User::isLoggedIn() const
{
    return d->isLoggedIn;
}
