#include "ldmlanguage.h"

using namespace QLightDM;

class LanguagePrivate
{
public:
    QString code;
    QString name;
    QString territory;
};

Language::Language(QString &code, QString &name, QString &territory)
    : d(new LanguagePrivate)
{
    d->code = code;
    d->name = name;
    d->territory = territory;
}

Language::Language(const Language &other)
    :d(new LanguagePrivate(*other.d))
{
}

Language::~Language()
{
    delete d;
}

Language& Language::operator=(const Language& other)
{
    *d = *other.d;
    return *this;
}


QString Language::code() const
{
    return d->code;
}

QString Language::name() const
{
    return d->name;
}

QString Language::territory() const
{
    return d->territory;
}


