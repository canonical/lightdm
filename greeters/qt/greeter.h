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


#ifndef GREETER_H
#define GREETER_H

#include <QWidget>

class LoginPrompt;
class Panel;

namespace QLightDM {
    class Greeter;
}

class Greeter : public QWidget
{
    Q_OBJECT
public:
    explicit Greeter();
    ~Greeter();

private slots:
    void onStartSession();
    
private:
    QLightDM::Greeter *m_greeter;
    LoginPrompt *m_prompt;
    Panel *m_panel;
};

#endif // GREETER_H
