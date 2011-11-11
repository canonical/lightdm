#include <QLightDM/Greeter>

class TestGreeter : public QLightDM::Greeter
{
    Q_OBJECT

public:
    TestGreeter ();

private slots:
    void showMessage(QString text, Greeter::MessageType type);
    void showPrompt(QString text, Greeter::PromptType type);
    void authenticationComplete();
};
