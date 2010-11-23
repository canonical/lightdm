#include "ldmsession.h"

#include <QDebug>

class LdmSessionPrivate
{
public:
    QString key;
    QString name;
    QString comment;
};

LdmSession::LdmSession(const QString &key, const QString &name, const QString &comment)
    : d(new LdmSessionPrivate)
{
    d->key = key;
    d->name = name;
    d->comment = comment;
}

LdmSession::LdmSession(const LdmSession &other)
    :d(new LdmSessionPrivate(*other.d))
{
}

LdmSession& LdmSession::operator=(const LdmSession& other)
{
    *d = *other.d;
    return *this;
}

LdmSession::~LdmSession()
{
    delete d;
}

QString LdmSession::key() const
{
    return d->key;
}

QString LdmSession::name() const
{
    return d->name;
}

QString LdmSession::comment() const
{
    return d->comment;
}
