#include "lightdm-qt-greeter.h"

#include <QtGui/QApplication>
#include <QtGui/QLabel>
#include <QtGui/QLineEdit>
#include <QtGui/QPushButton>
#include <QtGui/QGridLayout>

#include "ldmgreeter.h"

LoginDialog::LoginDialog() : QDialog()
{
    label = new QLabel("Username:", this);
    entry = new QLineEdit(this);
    connect(entry, SIGNAL(returnPressed()), this, SLOT(onLogin()));  

    QPushButton *button = new QPushButton("Login", this);
    connect(button, SIGNAL(clicked()), this, SLOT(onLogin()));

    QGridLayout *layout = new QGridLayout(this);
    layout->addWidget(label, 0, 0, 1, 1);
    layout->addWidget(entry, 1, 0, 2, 1);
    layout->addWidget(button, 2, 0, 3, 1);
    setLayout(layout);

    greeter = new LdmGreeter; //FIXME this LEAKS! Either finish the QWidget subclass plan, or add parent arg to LdmGreeter.
    connect(greeter, SIGNAL(showPrompt(QString)), this, SLOT(showPrompt(QString)));
    connect(greeter, SIGNAL(showMessage(QString)), this, SLOT(showMessage(QString)));
    connect(greeter, SIGNAL(showError(QString)), this, SLOT(showError(QString)));
    connect(greeter, SIGNAL(authenticationComplete()), this, SLOT(authenticationComplete()));
    connect(greeter, SIGNAL(quit()), this, SLOT(quit()));
    greeter->connectToServer();
}

void LoginDialog::onLogin()
{
    if(greeter->inAuthentication()) {
        if(inPrompt) {
            greeter->provideSecret(entry->text());
        }
        inPrompt = false;
        entry->setText("");
        entry->setEchoMode(QLineEdit::Normal);
    }
    else {
        greeter->startAuthentication(entry->text());
    }
}

void LoginDialog::showPrompt(QString text)
{
    entry->setText("");
    entry->setEchoMode(QLineEdit::Password);
    label->setText(text);
    inPrompt = true;
}

void LoginDialog::showMessage(QString text)
{    
    label->setText(text);
}

void LoginDialog::showError(QString text)
{
     label->setText(text);
}

void LoginDialog::authenticationComplete()
{
    entry->setText("");
    if(greeter->isAuthenticated()) {
        greeter->login(greeter->authenticationUser(), greeter->defaultSession(), greeter->defaultLanguage());
    }
    else {
        label->setText("Failed to authenticate");
    }
}

void LoginDialog::quit()
{
    exit(0);
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    LoginDialog d;
    d.show();

    return a.exec();
}
