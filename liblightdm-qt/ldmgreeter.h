#ifndef LDMGREETER_H
#define LDMGREETER_H

#include <QWidget>

class PowerManagementInterface;
class DisplayInterface;
class UserManagerInterface;

class LdmGreeterPrivate;
class LdmUser;
class LdmSession;
class LdmAuthRequest;
class LdmLanguage;
class QDBusPendingCallWatcher;

//TODO
// all accessors need to be marked const.
// need to pass by reference where applicable.
// fix FIXME about authentication.
// decide async start - provide ready() signal(like Tp-Qt4)?
// quit is a rubbish name for a signal, it sounds too much like a slot.
// maybe modify defaultLayout to return the layout?(same for sesion) - or the modelIndex?
// document all the public methods.

class Q_DECL_EXPORT LdmGreeter : public QWidget
{
    Q_OBJECT
public:
    explicit LdmGreeter();
    ~LdmGreeter();

    /** The hostname of the machine*/
    QString hostname();
    QString defaultLanguage(); //QLocale::Language?
    QString defaultLayout();
    QString defaultSession();
    QString defaultUsername();

    QList<LdmUser> users();
    QList<LdmSession> sessions();
  //  QList<LdmLanguage> languages();

    //FIXME this is inconsistent - need to decide whether lib remembers username, or client needs to keep passing it.
    void startAuthentication(QString username);
    void provideSecret(QString secret);
    void login(QString username, QString session, QString language);

    //FIXME should probably mess about with Q_PROPERTY
    bool canSuspend();
    bool canHibernate();
    bool canShutdown();
    bool canRestart();

    //FIXME replace these signals with pure virtual
    //virtual blah() = 0;

signals:
    void showPrompt(QString prompt);
    void showMessage(QString message);
    void showError(QString message);
    void authenticationComplete(bool success);
    void timedLogin(QString username);
    void quit();

public slots:
    void suspend();
    void hibernate();
    void shutdown();
    void restart();

private slots:
    void onAuthFinished(QDBusPendingCallWatcher*);

private:
    LdmGreeterPrivate* d;
};

#endif // GREETER_H
