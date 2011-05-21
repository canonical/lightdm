#ifndef LDMLANGUAGE_H
#define LDMLANGUAGE_H
#include <QString>

class LanguagePrivate;

namespace QLightDM {
    class Language
    {
    public:
        Language(QString &code, QString &name, QString &territory);
        ~Language();
        Language(const Language& other);
        Language &operator=(const Language& other);

        QString code() const;
        QString name() const;
        QString territory() const;
    private:
        LanguagePrivate* d;
    };
};

#endif // LDMLANGUAGE_H
