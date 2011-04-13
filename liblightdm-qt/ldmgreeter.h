#ifndef LDMGREETER_H
#define LDMGREETER_H

#include <QObject>

class LdmGreeterPrivate;
class LdmUser;
class LdmLanguage;
class LdmLayout;
class LdmSession;

class Q_DECL_EXPORT LdmGreeter : public QObject
{
    Q_OBJECT
public:
    LdmGreeter();
    ~LdmGreeter();

    /** The hostname of the machine */
    QString hostname();

    QString theme();
    QString getStringProperty(QString name);
    int getIntegerProperty(QString name);
    bool getBooleanProperty(QString name);

    QString timedLoginUser();
    int timedLoginDelay();

    QList<LdmUser> users();
    void getUserDefaults(QString name, QString language, QString layout, QString session);

    QList<LdmLanguage> languages();
    QString defaultLanguage();

    QList<LdmLayout> layouts();
    QString defaultLayout();
    QString layout();

    QList<LdmSession> sessions();
    QString defaultSession();

    bool inAuthentication();
    bool isAuthenticated();
    QString authenticationUser();

    void connectToServer();
    void cancelTimedLogin();  
    void startAuthentication(QString username);
    void provideSecret(QString secret);
    void cancelAuthentication();
    void login(QString username, QString session, QString language);

    bool canSuspend();
    bool canHibernate();
    bool canShutdown();
    bool canRestart();
    void suspend();
    void hibernate();
    void shutdown();
    void restart();

signals:
    void connected();
    void showPrompt(QString prompt);
    void showMessage(QString message);
    void showError(QString message);
    void authenticationComplete();
    void timedLogin(QString username);
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
};

#endif // LDMGREETER_H
