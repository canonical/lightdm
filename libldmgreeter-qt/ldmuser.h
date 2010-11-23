#ifndef LDMUSER_H
#define LDMUSER_H

#include <QString>
#include <QtDBus/QtDBus>

class LdmUserPrivate;

//public facing User class
class Q_DECL_EXPORT LdmUser
{
public:
    explicit LdmUser();
    LdmUser(const QString& name, const QString& realName, const QString& image, const bool loggedIn);
    LdmUser(const LdmUser& other);
    ~LdmUser();
    LdmUser &operator=(const LdmUser& other);

    /** The name to display (the real name if available, otherwise use the username */
    QString displayName() const;

    /** The username of the user*/
    QString name() const;
    /** The user's real name, use this for displaying*/
    QString realName() const;

    /** Returns the path to an avatar of this user*/
    QString image() const;

    /** Returns true if this user is already logged in on another session*/
    bool isLoggedIn() const;

//    LdmUser &operator=(const LdmUser user);
private:
    LdmUserPrivate* d;
};

QDBusArgument &operator<<(QDBusArgument &argument, const LdmUser &request);
const QDBusArgument &operator>>(const QDBusArgument &argument, LdmUser &request);

Q_DECLARE_METATYPE(LdmUser);
Q_DECLARE_METATYPE(QList<LdmUser>);

#endif // LDMUSER_H
