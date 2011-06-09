/*
 * Copyright (C) 2010-2011 David Edmundson.
 * Author: David Edmundson <kde@davidedmundson.co.uk>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

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
