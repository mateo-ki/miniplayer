#include <iostream>
#include <QGuiApplication>

#include "app/AppBootstrap.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& msg)
     {
         std::cout << msg.toStdString() << std::endl;
     });
    AppBootstrap bootstrap;
    if (!bootstrap.initialize()) {
        return -1;
    }

    return app.exec();
}
