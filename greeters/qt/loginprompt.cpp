#include "loginprompt.h"
#include "ui_loginprompt.h"

#include <QLightDM/Greeter>
#include <QLightDM/User>
#include <QLightDM/Language>

#include <QListWidgetItem>
#include <QLightDM/usersmodel.h>

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
    if (ui->userListView->currentIndex().isValid()) {
        m_greeter->startAuthentication(ui->userListView->currentIndex().data(Qt::UserRole).toString());
    }
}

void LoginPrompt::onAuthenticationComplete(bool success)
{
    if (success) {
        ui->feedbackLabel->setText("YAY - log in");
        //        m_greeter->login(ui->userList->currentItem()->text(), "kde", "en-UK");
    } else {
        ui->feedbackLabel->setText("Sorry, you suck. Try again.");
    }
}

void LoginPrompt::prompt(const QString &message)
{
    qDebug() << message;
    m_greeter->provideSecret(ui->password->text());
}
