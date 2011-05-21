#ifndef LOGINPROMPT_H
#define LOGINPROMPT_H

#include <QWidget>

namespace Ui {
    class Widget;
}

class LdmGreeter;

class LoginPrompt : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPrompt(LdmGreeter* greeter, QWidget *parent = 0);
    virtual ~LoginPrompt();

private slots:
    void onLoginButtonClicked();
    void onAuthenticationComplete(bool success);
    void prompt(const QString &message);

private:
    LdmGreeter *m_greeter;
    Ui::Widget *ui;
};

#endif // WIDGET_H
