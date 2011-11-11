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

        QString timedLoginUser() const;
        int timedLoginDelay() const;

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

    public slots:
        bool connectSync();
        void authenticate(const QString &username=QString());
        void authenticateAsGuest();
        void respond(const QString &response);
        void cancelAuthentication();
        void setLanguage (QString language);
        bool startSessionSync(const QString &session=QString());

    signals:
        void showMessage(QString text, QLightDM::MessageType type);
        void showPrompt(QString text, QLightDM::PromptType type);
        void authenticationComplete();
        void autologinTimerExpired();

    private slots:
        void onRead(int fd);

    private:
        GreeterPrivate *d;

    };
};

#endif // QLIGHTDM_GREETER_H
