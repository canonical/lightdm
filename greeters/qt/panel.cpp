#include "panel.h"
#include "ui_panel.h"

#include <lightdm-qt-0/lightdm/LdmGreeter>
#include <lightdm-qt-0/lightdm/LdmSessionsModel>

#include <QMenu>
#include <QAction>
#include <QIcon>

Panel::Panel(LdmGreeter *greeter, QWidget *parent):
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

    ui->sessionCombo->setModel(m_greeter->sessionsModel());
}

Panel::~Panel()
{
    delete ui;
}
