#include <QLightDM/Greeter>

class TestGreeter : public QLightDM::Greeter
{
    Q_OBJECT

public:
    TestGreeter ();

private slots:
    void showMessage(QString text, QLightDM::MessageType type);
    void showPrompt(QString text, QLightDM::PromptType type);
    void authenticationComplete();
};
