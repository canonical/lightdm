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

#include <QLightDM/Greeter>
#include <QLightDM/User>
#include <QLightDM/Language>
#include <QLightDM/UsersModel>

#include <QtCore/QDebug>
#include <QtGui/QListWidgetItem>

LoginPrompt::LoginPrompt(QLightDM::Greeter *greeter, QWidget *parent) :
    QWidget(parent),
    m_greeter(greeter),
    ui(new Ui::Widget)
{
    ui->setupUi(this);
    ui->feedbackLabel->setText(QString());
    
    ui->hostnameLabel->setText(m_greeter->hostname());
    
    QLightDM::UsersModel *usersModel = new QLightDM::UsersModel(greeter->config(), this);
    ui->userListView->setModel(usersModel);

    connect(ui->loginButton, SIGNAL(released()), SLOT(onLoginButtonClicked()));
    connect(m_greeter, SIGNAL(authenticationComplete(bool)), SLOT(onAuthenticationComplete(bool)));
    connect(m_greeter, SIGNAL(showPrompt(QString)), SLOT(prompt(QString)));
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
        m_greeter->login(currentIndex.data(QLightDM::UsersModel::NameRole).toString());
    }
}

void LoginPrompt::onAuthenticationComplete(bool success)
{
    if (success) {
        emit startSession();
    } else {
        ui->feedbackLabel->setText("Sorry, you suck. Try again.");
    }
}

void LoginPrompt::prompt(const QString &message) {
    qDebug() << message;
    m_greeter->respond(ui->password->text());
}
