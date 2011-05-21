#include <QtGui/QApplication>

#include <QDebug>
#include <QFile>

#include "greeter.h"

//temp code to redirect qDebug to a file which can be handy for real debugging.
void messageHandler(QtMsgType type, const char *msg)
{
    QString txt;
    switch (type) {
    case QtDebugMsg:
        txt = QString("Debug: %1").arg(msg);
        break;
    case QtWarningMsg:
        txt = QString("Warning: %1").arg(msg);
        break;
    case QtCriticalMsg:
        txt = QString("Critical: %1").arg(msg);
        break;
    case QtFatalMsg:
        txt = QString("Fatal: %1").arg(msg);
        abort();
    }

    QFile outFile("/home/david/temp/log");
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream ts(&outFile);
    ts << txt << endl;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
   // qInstallMsgHandler(messageHandler);

    Greeter mainUi;
    mainUi.show();

    return app.exec();
}
