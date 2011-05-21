#ifndef GREETER_H
#define GREETER_H

#include <QWidget>

class LoginPrompt;

class Greeter : public QWidget
{
    Q_OBJECT
public:
    explicit Greeter();
    ~Greeter();

private:
};

#endif // GREETER_H
