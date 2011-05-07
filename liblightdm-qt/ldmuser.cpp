#include "ldmuser.h"

class LdmUserPrivate
{
public:
    QString name;
    QString realName;
    QString homeDirectory;
    QString image;
    bool isLoggedIn;
};

LdmUser::LdmUser():
    d(new LdmUserPrivate)
{
}

LdmUser::LdmUser(const QString& name, const QString& realName, const QString& homeDirectory, const QString& image, bool isLoggedIn) :
    d(new LdmUserPrivate)
{
    d->name = name;
    d->realName = realName;
    d->homeDirectory = homeDirectory;
    d->image = image;
    d->isLoggedIn = isLoggedIn;
}

LdmUser::LdmUser(const LdmUser &other)
    :d(new LdmUserPrivate(*other.d))
{
}

LdmUser::~LdmUser()
{
    delete d;
}


LdmUser& LdmUser::operator=(const LdmUser& other)
{
    *d = *other.d;
    return *this;
}

bool LdmUser::update(const QString& realName, const QString& homeDirectory, const QString& image, bool isLoggedIn)
{
    if (d->realName == realName && d->homeDirectory == homeDirectory && d->image == image && d->isLoggedIn == isLoggedIn)
        return false;

    d->realName = realName;
    d->homeDirectory = homeDirectory;
    d->image = image;
    d->isLoggedIn = isLoggedIn;

    return true;
}

QString LdmUser::displayName() const
{
    if (!d->realName.isEmpty())
    {
        return d->realName;
    }
    else
    {
        return d->name;
    }
}

QString LdmUser::name() const
{
    return d->name;
}

QString LdmUser::realName() const
{
    return d->realName;
}

QString LdmUser::homeDirectory() const
{
    return d->homeDirectory;
}

QString LdmUser::image() const
{
    return d->image;
}

bool LdmUser::isLoggedIn() const
{
    return d->isLoggedIn;
}
