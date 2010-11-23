#include "ldmauthrequest.h"
#include <QtDBus/QDBusArgument>
#include <QList>
#include <QDebug>



class LdmAuthRequestPrivate
{
public:
    //rename when I find out what the hell these contain
    int messageType;
    QString message;
};

LdmAuthRequest::LdmAuthRequest()
    :d(new LdmAuthRequestPrivate)
{
}


LdmAuthRequest::LdmAuthRequest(const int messageType, const QString& message)
    :d(new LdmAuthRequestPrivate)
{
    d->messageType = messageType;
    d->message = message;
}

LdmAuthRequest::LdmAuthRequest(const LdmAuthRequest &other)
    :d(new LdmAuthRequestPrivate(*other.d))
{

}

LdmAuthRequest::~LdmAuthRequest()
{
    delete d;
}

LdmAuthRequest& LdmAuthRequest::operator =(const LdmAuthRequest &other)
{
    *d = *other.d;
    return *this;
}


int LdmAuthRequest::messageType() const
{
    return d->messageType;
}

QString LdmAuthRequest::message() const
{
    return d->message;
}


QDBusArgument &operator<<(QDBusArgument &argument, const LdmAuthRequest &request)
{
    argument.beginStructure();
    argument << request.messageType() << request.message();
    argument.endStructure();

    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, LdmAuthRequest &request)
{
    int messageType;
    QString message;
    argument.beginStructure();
    argument >> messageType >> message;
    argument.endStructure();

    request = LdmAuthRequest(messageType, message);
    return argument;
}
