#ifndef LDMSESSION_H
#define LDMSESSION_H

#include <QString>

class LdmSessionPrivate;

class Q_DECL_EXPORT LdmSession
{
public:
    LdmSession(const QString& key, const QString &name, const QString &comment);
    LdmSession(const LdmSession& other);
    ~LdmSession();
    LdmSession &operator=(const LdmSession& other);

    QString key() const;
    QString name() const;
    QString comment() const;

private:
    LdmSessionPrivate *d;

};

#endif // LDMSESSION_H
