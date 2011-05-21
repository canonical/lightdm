#ifndef QLIGTHDM_GREETER_H
#define QLIGTHDM_GREETER_H

#include <QObject>
class GreeterPrivate;

#include "user.h"
#include "language.h"
//#include "ldmlayout.h"

class GreeterPrivate;

namespace QLightDM
{
  class SessionsModel;
  
  class Q_DECL_EXPORT Greeter : public QObject
  {
    Q_OBJECT
    public:
        explicit Greeter(QObject* parent=0);
        virtual ~Greeter();

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

	QList<QLightDM::User*> users();

	QList<QLightDM::Language> languages() const;
	QString defaultLanguage() const;

	//QList<LdmLayout> layouts() const;
	QString defaultLayout() const;
	QString layout() const;

	QLightDM::SessionsModel *sessionsModel() const;
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
        void userAdded(User *user);
        void userChanged(User *user);
        void userRemoved(User *user);
	void quit();
    
    private slots:  
	void onRead(int fd);

    private:
	GreeterPrivate *d;
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

};//end namespace

#endif // LDMGREETER_H
