#ifndef PANEL_H
#define PANEL_H

#include <QWidget>

namespace Ui {
    class Panel;
}

namespace QLightDM {
    class Greeter;
}

class Panel : public QWidget
{
    Q_OBJECT

public:
    explicit Panel(QLightDM::Greeter *greeter, QWidget *parent = 0);
    virtual ~Panel();
    
    /** Returns the currently selected session*/
    QString session() const;
    
    
private:
    Ui::Panel *ui;
    QLightDM::Greeter *m_greeter;
};

#endif // PANEL_H
