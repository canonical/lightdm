#ifndef LDMAUTHREQUEST_H
#define LDMAUTHREQUEST_H

#include <QString>
#include <QtDBus/QtDBus>

class LdmAuthRequestPrivate;

//FIXME this is never public facing, remove Ldm prefix - make sure it's not exported.
class LdmAuthRequest
{
public:
    explicit LdmAuthRequest();
    LdmAuthRequest(const int messageType, const QString& message);
    LdmAuthRequest(const LdmAuthRequest& other);
    ~LdmAuthRequest();

    LdmAuthRequest &operator=(const LdmAuthRequest& other);

    int messageType() const;
    QString message() const;


//    LdmAuthRequest &operator=(const LdmAuthRequest user);
private:
    LdmAuthRequestPrivate* d;
};

QDBusArgument &operator<<(QDBusArgument &argument, const LdmAuthRequest &request);
const QDBusArgument &operator>>(const QDBusArgument &argument, LdmAuthRequest &request);

Q_DECLARE_METATYPE(LdmAuthRequest);
Q_DECLARE_METATYPE(QList<LdmAuthRequest>);

#endif // LDMAUTHREQUEST_H
