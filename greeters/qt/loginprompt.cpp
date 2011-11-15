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

#include "loginprompt.h"
#include "ui_loginprompt.h"

#include <QLightDM/User>
#include <QLightDM/User>
#include <QLightDM/System>

#include <QtCore/QDebug>
#include <QtGui/QListWidgetItem>

LoginPrompt::LoginPrompt(QLightDM::Greeter *greeter, QWidget *parent) :
    QWidget(parent),
    m_greeter(greeter),
    ui(new Ui::Widget)
{
    ui->setupUi(this);
    ui->feedbackLabel->setText(QString());
    
    ui->hostnameLabel->setText(QLightDM::System::hostname());
    
    ui->userListView->setModel(QLightDM::users());

    connect(ui->loginButton, SIGNAL(released()), SLOT(onLoginButtonClicked()));
    connect(m_greeter, SIGNAL(authenticationComplete()), SLOT(onAuthenticationComplete()));
    connect(m_greeter, SIGNAL(showPrompt(QString, QLightDM::Greeter::PromptType)), SLOT(prompt(QString, QLightDM::Greeter::PromptType)));
}

LoginPrompt::~LoginPrompt()
{
    delete ui;
}

void LoginPrompt::onLoginButtonClicked()
{
    ui->feedbackLabel->setText(QString());
    QModelIndex currentIndex = ui->userListView->currentIndex();
    if (currentIndex.isValid()) {
        m_greeter->authenticate(currentIndex.data(QLightDM::UsersModel::NameRole).toString());
    }
}

void LoginPrompt::onAuthenticationComplete()
{
    if (m_greeter->isAuthenticated()) {
        emit startSession();
    } else {
        ui->feedbackLabel->setText("Incorrect password, please try again");
    }
}

void LoginPrompt::prompt(const QString &text, QLightDM::Greeter::PromptType type) {
    qDebug() << text;
    m_greeter->respond(ui->password->text());
}
