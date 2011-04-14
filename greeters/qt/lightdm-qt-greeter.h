#include <QtGui>

#include "ldmgreeter.h"

class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    LoginDialog();

private:
    LdmGreeter *greeter;
    QLabel *label;
    QLineEdit *entry;
    bool inPrompt;

private slots:
    void onLogin();
    void showPrompt(QString text);
    void showMessage(QString text);
    void showError(QString text);
    void authenticationComplete();
    void quit();
};
