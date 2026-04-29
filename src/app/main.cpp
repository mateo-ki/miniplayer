#include <QGuiApplication>

#include "app/AppBootstrap.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    AppBootstrap bootstrap;
    if (!bootstrap.initialize()) {
        return -1;
    }

    return app.exec();
}
