#include "ldmuser.h"

class LdmUserPrivate
{
public:
    QString name;
    QString realName;
    QString image;
    bool isLoggedIn;
};

LdmUser::LdmUser():
    d(new LdmUserPrivate)
{
}

LdmUser::LdmUser(const QString& name, const QString& realName, const QString& image, const bool loggedIn) :
    d(new LdmUserPrivate)
{
    d->name = name;
    d->realName = realName;
    d->image = image;
    d->isLoggedIn = loggedIn;
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

QString LdmUser::image() const
{
    return d->image;
}

bool LdmUser::isLoggedIn() const
{
    return d->isLoggedIn;
}


//don't actually need this I never send an LdmUser across DBUS...
QDBusArgument &operator<<(QDBusArgument &argument, const LdmUser &user)
{
    argument.beginStructure();
    argument << user.name() << user.realName() << user.image() << user.isLoggedIn();
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, LdmUser &user)
{
    QString name;
    QString realName;
    QString image;
    bool loggedIn;

    argument.beginStructure();
    argument >> name >> realName >> image >> loggedIn;
    argument.endStructure();

    user  = LdmUser(name, realName, image, loggedIn);

    return argument;
}

