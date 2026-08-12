#include <iostream>
#include <QApplication>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "app/AppBootstrap.h"
#include "infrastructure/Logger.h"

int main(int argc, char *argv[]) {
#ifdef _MSC_VER
    // Disable CRT heap debug checks to avoid false heap corruption reports
    // from FFmpeg's internal memory management in Debug builds
    _CrtSetDbgFlag(0);
#endif

    QApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg) {
        std::cout << msg.toStdString() << std::endl;
        switch (type) {
        case QtDebugMsg:
        case QtInfoMsg:
            Logger::instance().info(QStringLiteral("[Qt] ") + msg);
            break;
        case QtWarningMsg:
            Logger::instance().warn(QStringLiteral("[Qt] ") + msg);
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            Logger::instance().error(QStringLiteral("[Qt] ") + msg);
            break;
        }
    });
    AppBootstrap bootstrap;
    if (!bootstrap.initialize()) {
        Logger::instance().error(QStringLiteral("Application bootstrap failed"));
        return -1;
    }

    return app.exec();
}
