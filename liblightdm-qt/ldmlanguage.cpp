#include "ldmlanguage.h"

class LdmLanguagePrivate
{
public:
    QString code;
    QString name;
    QString territory;
};

LdmLanguage::LdmLanguage(QString &code, QString &name, QString &territory)
    : d(new LdmLanguagePrivate)
{
    d->code = code;
    d->name = name;
    d->territory = territory;
}

LdmLanguage::LdmLanguage(const LdmLanguage &other)
    :d(new LdmLanguagePrivate(*other.d))
{
}

LdmLanguage::~LdmLanguage()
{
    delete d;
}

LdmLanguage& LdmLanguage::operator=(const LdmLanguage& other)
{
    *d = *other.d;
    return *this;
}


QString LdmLanguage::code() const
{
    return d->code;
}

QString LdmLanguage::name() const
{
    return d->name;
}

QString LdmLanguage::territory() const
{
    return d->territory;
}


