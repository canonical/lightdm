#ifndef LOGINPROMPT_H
#define LOGINPROMPT_H

#include <QWidget>

namespace Ui {
    class Widget;
}

namespace QLightDM {
    class Greeter;
}

class LoginPrompt : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPrompt(QLightDM::Greeter* greeter, QWidget *parent = 0);
    virtual ~LoginPrompt();

private slots:
    void onLoginButtonClicked();
    void onAuthenticationComplete(bool success);
    void prompt(const QString &message);

private:
    QLightDM::Greeter *m_greeter;
    Ui::Widget *ui;
};

#endif // WIDGET_H
