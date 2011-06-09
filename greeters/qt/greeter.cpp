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


#include "greeter.h"

#include <QLabel>
#include <QApplication>
#include <QDesktopWidget>
#include <QDebug>

#include <QLightDM/Greeter>

#include "loginprompt.h"
#include "panel.h"

Greeter::Greeter() :
    QWidget(0)
{
    QRect screen = QApplication::desktop()->rect();
    setGeometry(screen);

    QLabel *background = new QLabel(this);

    //TODO load this from the config file in order to test that works.
    background->setPixmap(QPixmap("/usr/share/wallpapers/Horos/contents/images/1920x1200.png"));

    m_greeter = new QLightDM::Greeter(this);
    m_greeter->connectToServer();
    connect(m_greeter, SIGNAL(quit()), this, SLOT(close()));

    m_prompt = new LoginPrompt(m_greeter, this);
    m_prompt->move(this->width()/2 - m_prompt->width()/2, this->height()/2 - m_prompt->height()/2);
    m_prompt->setAutoFillBackground(true);
    connect(m_prompt, SIGNAL(startSession()), SLOT(onStartSession()));

    m_panel = new Panel(m_greeter, this);
    m_panel->setGeometry(QRect(QPoint(0, screen.height() - m_panel->height()), screen.bottomRight()));
    m_panel->setAutoFillBackground(true);
}


Greeter::~Greeter()
{
}

void Greeter::onStartSession() {
    m_greeter->startSession(m_panel->session());
}