#ifndef GREETER_H
#define GREETER_H

#include <QWidget>

class LoginPrompt;
class Panel;

namespace QLightDM {
    class Greeter;
}

class Greeter : public QWidget
{
    Q_OBJECT
public:
    explicit Greeter();
    ~Greeter();

private slots:
    void onStartSession();
    
private:
    QLightDM::Greeter *m_greeter;
    LoginPrompt *m_prompt;
    Panel *m_panel;
};

#endif // GREETER_H
