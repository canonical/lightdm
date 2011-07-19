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

#ifndef QLIGHTDM_GREETER_H
#define QLIGHTDM_GREETER_H

#include <QtCore/QObject>
#include <QtCore/QVariant>
#include "QLightDM/User"
#include "QLightDM/Language"

class GreeterPrivate;

namespace QLightDM
{
  typedef enum
  {
    PROMPT_TYPE_QUESTION,
    PROMPT_TYPE_SECRET
  } PromptType;

  typedef enum
  {
    MESSAGE_TYPE_INFO,
    MESSAGE_TYPE_ERROR
  } MessageType;

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

        Q_PROPERTY(QString hostname READ hostname CONSTANT);

        /** The hostname of the machine */
        QString hostname() const;

        QString timedLoginUser() const;
        int timedLoginDelay() const;

        QList<QLightDM::Language> languages() const;
        QString defaultLanguage() const;

        QString layout() const;

        QString getHint(QString name) const;
        QString defaultSessionHint() const;
        bool hideUsersHint() const;
        bool hasGuestAccountHint() const;
        QString selectUserHint() const;
        bool selectGuestHint() const;
        QString autologinUserHint() const;
        bool autologinGuestHint() const;
        int autologinTimeoutHint() const;

        bool inAuthentication() const;
        bool isAuthenticated() const;
        QString authenticationUser() const;

        bool canSuspend() const;
        bool canHibernate() const;
        bool canShutdown() const;
        bool canRestart() const;

    public slots:
        void suspend();
        void hibernate();
        void shutdown();
        void restart();

        void connectToServer();
        void login(const QString &username=QString());
        void loginAsGuest();
        void respond(const QString &response);
        void cancelAuthentication();
        void startSession(const QString &session=QString());

    signals:
        void connected();
        void showPrompt(QString prompt, PromptType type);
        void showMessage(QString message, MessageType type);
        void authenticationComplete();
        void sessionFailed();
        void autologinTimerExpired();
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
};

#endif // QLIGHTDM_GREETER_H
