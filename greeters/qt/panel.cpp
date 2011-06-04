#include "panel.h"
#include "ui_panel.h"

#include <QLightDM/Greeter>
#include <QLightDM/SessionsModel>

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

    QAction *shutDownAction = new QAction(QIcon::fromTheme("system-shutdown"), "Shutdown", this);
    connect(shutDownAction, SIGNAL(triggered()), m_greeter, SLOT(shutdown()));
    shutDownAction->setEnabled(m_greeter->canShutdown());
    powerMenu->addAction(shutDownAction);

    QAction *restartAction = new QAction(QIcon::fromTheme("system-reboot"), "Restart", this);
    connect(restartAction, SIGNAL(triggered()), m_greeter, SLOT(restart()));
    restartAction->setEnabled(m_greeter->canRestart());
    powerMenu->addAction(restartAction);

    QAction* suspendAction = new QAction(QIcon::fromTheme("system-suspend"), "Suspend", this);
    connect(suspendAction, SIGNAL(triggered()), m_greeter, SLOT(suspend()));
    suspendAction->setEnabled(m_greeter->canSuspend());
    powerMenu->addAction(suspendAction);

    QAction* hibernateAction = new QAction(QIcon::fromTheme("system-suspend-hibernate"), "Hibernate", this);
    connect(hibernateAction, SIGNAL(triggered()), m_greeter, SLOT(hibernate()));
    hibernateAction->setEnabled(m_greeter->canHibernate());
    powerMenu->addAction(hibernateAction);

    ui->powerOptionsButton->setMenu(powerMenu);

    
    QLightDM::SessionsModel* sessionsModel = new QLightDM::SessionsModel(this);
    ui->sessionCombo->setModel(sessionsModel);
}

QString Panel::session() const{
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
