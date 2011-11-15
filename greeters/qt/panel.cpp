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

#include "panel.h"
#include "ui_panel.h"

#include <QLightDM/Greeter>
#include <QLightDM/Session>
#include <QLightDM/Power>

#include <QMenu>
#include <QAction>
#include <QIcon>

Panel::Panel(QLightDM::Greeter *greeter, QWidget *parent):
    m_greeter(greeter),
    QWidget(parent),
    ui(new Ui::Panel)
{
    ui->setupUi(this);

    ui->powerOptionsButton->setText(QString());
    ui->powerOptionsButton->setIcon(QIcon::fromTheme("system-shutdown"));

    QMenu *powerMenu = new QMenu(this);

    QLightDM::PowerInterface* powerInterface = new QLightDM::PowerInterface(this);

    QAction *shutDownAction = new QAction(QIcon::fromTheme("system-shutdown"), "Shutdown", this);
    connect(shutDownAction, SIGNAL(triggered()), powerInterface, SLOT(shutdown()));
    shutDownAction->setEnabled(powerInterface->canShutdown());
    powerMenu->addAction(shutDownAction);

    QAction *restartAction = new QAction(QIcon::fromTheme("system-reboot"), "Restart", this);
    connect(restartAction, SIGNAL(triggered()), powerInterface, SLOT(restart()));
    restartAction->setEnabled(powerInterface->canRestart());
    powerMenu->addAction(restartAction);

    QAction* suspendAction = new QAction(QIcon::fromTheme("system-suspend"), "Suspend", this);
    connect(suspendAction, SIGNAL(triggered()), powerInterface, SLOT(suspend()));
    suspendAction->setEnabled(powerInterface->canSuspend());
    powerMenu->addAction(suspendAction);

    QAction* hibernateAction = new QAction(QIcon::fromTheme("system-suspend-hibernate"), "Hibernate", this);
    connect(hibernateAction, SIGNAL(triggered()), powerInterface, SLOT(hibernate()));
    hibernateAction->setEnabled(powerInterface->canHibernate());
    powerMenu->addAction(hibernateAction);

    ui->powerOptionsButton->setMenu(powerMenu);    
    ui->sessionCombo->setModel(QLightDM::sessions());
}

QString Panel::session() const
{
    int index = ui->sessionCombo->currentIndex();
    if (index > -1) {
        return ui->sessionCombo->itemData(index, QLightDM::SessionsModel::IdRole).toString();
    }
    return QString();
}


Panel::~Panel()
{
    delete ui;
}
