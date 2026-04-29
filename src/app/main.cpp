#include <QGuiApplication>

#include "app/AppBootstrap.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    AppBootstrap bootstrap;
    bootstrap.initialize();

    return app.exec();
}
