/*
 * Copyright (C) 2010-2011 David Edmundson.
 * Copyright (C) 2010-2011 Robert Ancell
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 * 
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version. See http://www.gnu.org/copyleft/lgpl.html the full text of the
 * license.
 */

#ifndef QLIGTHDM_GREETER_H
#define QLIGTHDM_GREETER_H

#include <QObject>

#include "user.h"
#include "language.h"
//#include "ldmlayout.h"

class GreeterPrivate;

namespace QLightDM
{
  class Config;
  
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

	QList<QLightDM::Language> languages() const;
	QString defaultLanguage() const;

	//QList<LdmLayout> layouts() const;
	QString layout() const;

    QLightDM::Config *config() const;

	QString defaultSession() const;
    bool guestAccountSupported() const;
    bool isFirst() const;

	bool inAuthentication() const;
	bool isAuthenticated() const;
	QString authenticationUser() const;

	void connectToServer();
	void cancelTimedLogin();  
	void login(const QString &username);
	void loginAsGuest();
	void provideSecret(const QString &secret);
	void cancelAuthentication();
	void startSession(const QString &session=QString(), const QString &language=QString());

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
    };

};//end namespace

#endif // LDMGREETER_H
