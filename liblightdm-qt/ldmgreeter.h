#ifndef LDMGREETER_H
#define LDMGREETER_H

#include <QObject>

class LdmGreeterPrivate;

#include "ldmuser.h"
#include "ldmlanguage.h"
//#include "ldmlayout.h"

class LdmSessionsModel;

class Q_DECL_EXPORT LdmGreeter : public QObject
{
    Q_OBJECT
public:

    explicit LdmGreeter(QObject* parent=0);
    virtual ~LdmGreeter();

    Q_PROPERTY(bool canSuspend READ canSuspend);
    Q_PROPERTY(bool canHibernate READ canHibernate);
    Q_PROPERTY(bool canShutdown READ canShutdown);
    Q_PROPERTY(bool canRestart READ canRestart);

    Q_PROPERTY(QString hostname READ hostname);

    /** The hostname of the machine */
    QString hostname() const;
    QString theme() const;

    QVariant getProperty(const QString &name) const;

    QString timedLoginUser() const;
    int timedLoginDelay() const;

    QList<LdmUser*> users();
    void getUserDefaults(const QString &name, const QString &language, const QString &layout, const QString &session);

    QList<LdmLanguage> languages() const;
    QString defaultLanguage() const;

    //QList<LdmLayout> layouts() const;
    QString defaultLayout() const;
    QString layout() const;

    LdmSessionsModel *sessionsModel() const;
    QString defaultSession() const;

    bool inAuthentication() const;
    bool isAuthenticated() const;
    QString authenticationUser() const;

    void connectToServer();
    void cancelTimedLogin();  
    void startAuthentication(const QString &username);
    void provideSecret(const QString &secret);
    void cancelAuthentication();
    void login(const QString &username, const QString &session, const QString &language);
    void loginWithDefaults(const QString &username);

    bool canSuspend() const;
    bool canHibernate() const;
    bool canShutdown() const;
    bool canRestart() const;

public slots:
    void suspend();
    void hibernate();
    void shutdown();
    void restart();
  
signals:
    void connected();
    void showPrompt(QString prompt);
    void showMessage(QString message);
    void showError(QString message);
    void authenticationComplete(bool isAuthenticated);
    void timedLogin(QString username);
    void userAdded(LdmUser *user);
    void userChanged(LdmUser *user);
    void userRemoved(LdmUser *user);
    void quit();
 
private slots:  
    void onRead(int fd);

private:
    LdmGreeterPrivate *d;
    void writeInt(int value);
    void writeString(QString value);
    void writeHeader(int id, int length);
    void flush();
    int getPacketLength();
    int readInt(int *offset);
    QString readString(int *offset);
    void loadConfig();
    void loadUsers();
    void updateUsers();
};

#endif // LDMGREETER_H
