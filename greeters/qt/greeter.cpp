#include "greeter.h"

#include <QLabel>
#include <QApplication>
#include <QDesktopWidget>

#include <LdmGreeter>

#include "loginprompt.h"
#include "panel.h"


Greeter::Greeter() :
    QWidget(0)
{
    QRect screen = QApplication::desktop()->rect();
    setGeometry(screen);

    QLabel *background = new QLabel(this);

    //TODO load this from the config file in order to test that works.
    background->setPixmap(QPixmap("/usr/share/wallpapers/Horos/contents/images/1920x1200.png"));

    LdmGreeter* greeter = new LdmGreeter(this);
    greeter->connectToServer();

    LoginPrompt* loginPrompt = new LoginPrompt(greeter, this);
    loginPrompt->move(this->width()/2 - loginPrompt->width()/2, this->height()/2 - loginPrompt->height()/2);
    loginPrompt->setAutoFillBackground(true);

    Panel* panel = new Panel(greeter, this);
    panel->setGeometry(QRect(QPoint(0, screen.height() - panel->height()), screen.bottomRight()));
    panel->setAutoFillBackground(true);
}


Greeter::~Greeter()
{
}
