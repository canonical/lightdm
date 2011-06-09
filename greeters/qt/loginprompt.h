/*
 * Copyright (C) 2010-2011 David Edmundson.
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */


#ifndef LOGINPROMPT_H
#define LOGINPROMPT_H

#include <QWidget>

namespace Ui {
    class Widget;
}

namespace QLightDM {
    class Greeter;
}

class LoginPrompt : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPrompt(QLightDM::Greeter* greeter, QWidget *parent = 0);
    virtual ~LoginPrompt();

signals:
    void startSession();
    
private slots:
    void onLoginButtonClicked();
    void onAuthenticationComplete(bool success);
    void prompt(const QString &message);

private:
    QLightDM::Greeter *m_greeter;
    Ui::Widget *ui;
};

#endif // WIDGET_H
