#include <QLightDM/Greeter>
#include <QLightDM/UsersModel>

class TestGreeter : public QLightDM::Greeter
{
    Q_OBJECT

public:
    TestGreeter ();

private Q_SLOTS:
    void showMessage(QString text, QLightDM::Greeter::MessageType type);
    void showPrompt(QString text, QLightDM::Greeter::PromptType type);
    void authenticationComplete();
    void autologinTimerExpired();
    void userRowsInserted(const QModelIndex & parent, int start, int end);
    void userRowsRemoved(const QModelIndex & parent, int start, int end);
};
