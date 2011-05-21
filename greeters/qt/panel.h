#ifndef PANEL_H
#define PANEL_H

#include <QWidget>

namespace Ui {
    class Panel;
}

class LdmGreeter;

class Panel : public QWidget
{
    Q_OBJECT

public:
    explicit Panel(LdmGreeter *greeter, QWidget *parent = 0);
    virtual ~Panel();

private:
    Ui::Panel *ui;
    LdmGreeter *m_greeter;
};

#endif // PANEL_H
