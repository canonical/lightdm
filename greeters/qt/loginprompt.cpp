#include "loginprompt.h"
#include "ui_loginprompt.h"

#include <QLightDM/Greeter>
#include <QLightDM/User>
#include <QLightDM/Session>
#include <QLightDM/Language>

#include <QListWidgetItem>

LoginPrompt::LoginPrompt(LdmGreeter *greeter, QWidget *parent) :
    QWidget(parent),
    m_greeter(greeter),
    ui(new Ui::Widget)
{
    ui->setupUi(this);
    ui->feedbackLabel->setText(QString());

    m_greeter->connectToServer();

    ui->hostnameLabel->setText(m_greeter->hostname());

    QList<LdmUser*> users = m_greeter->users();
    foreach(LdmUser *user, users) {
        QListWidgetItem* item = new QListWidgetItem(user->displayName(), ui->userList);
        item->setData(Qt::UserRole, user->name());
        if(user->image().isEmpty())         {
            item->setIcon(QIcon::fromTheme("user-identity"));
        } else {
            item->setIcon(QIcon(user->image()));
        }

    }

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
    if (ui->userList->currentItem()) {
        m_greeter->startAuthentication(ui->userList->currentItem()->data(Qt::UserRole).toString());
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
