#ifndef QLIGTHDM_USER_H
#define QLIGTHDM_USER_H

#include <QString>
#include <QtDBus/QtDBus>

class UserPrivate;

namespace QLightDM
{
    //public facing User class
    class Q_DECL_EXPORT User
    {
    public:
        explicit User();
        User(const QString &name, const QString &realName, const QString &homeDirectory, const QString &image, bool isLoggedIn);
        ~User();

        bool update(const QString &realName, const QString &homeDirectory, const QString &image, bool isLoggedIn);

        /** The name to display (the real name if available, otherwise use the username */
        QString displayName() const;

        /** The username of the user*/
        QString name() const;
        /** The user's real name, use this for displaying*/
        QString realName() const;

        /** Returns the home directory of this user*/
        QString homeDirectory() const;

        /** Returns the path to an avatar of this user*/
        QString image() const;

        /** Returns true if this user is already logged in on another session*/
        bool isLoggedIn() const;

    private:
        Q_DISABLE_COPY(User);
        UserPrivate* d;
    };
}

Q_DECLARE_METATYPE(QLightDM::User*);

#endif // LDMUSER_H
