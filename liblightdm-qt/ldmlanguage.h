#ifndef LDMLANGUAGE_H
#define LDMLANGUAGE_H
#include <QString>

class LdmLanguagePrivate;

class LdmLanguage
{
public:
    LdmLanguage(QString &code, QString &name, QString &territory);
    ~LdmLanguage();
    LdmLanguage(const LdmLanguage& other);
    LdmLanguage &operator=(const LdmLanguage& other);

    QString code() const;
    QString name() const;
    QString territory() const;
private:
    LdmLanguagePrivate* d;
};

#endif // LDMLANGUAGE_H
