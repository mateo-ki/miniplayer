#include <iostream>
#include <QApplication>
#include <QLockFile>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>

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

    // Keep a single MeloBox process per user session. QLockFile also removes
    // stale locks left behind by a crashed process, so a forced termination
    // will not permanently prevent the next launch.
    QLockFile instanceLock(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/MeloBox.instance.lock"));
    instanceLock.setStaleLockTime(30000);
    if (!instanceLock.tryLock(0)) {
        bool activationSent = false;
        // The first process may still be loading QML before its activation
        // server starts listening. Retry briefly so a rapid second launch is
        // still guaranteed to focus the already-running window.
        for (int attempt = 0; attempt < 5 && !activationSent; ++attempt) {
            QLocalSocket activationSocket;
            activationSocket.connectToServer(AppBootstrap::instanceServerName(), QIODevice::WriteOnly);
            if (activationSocket.waitForConnected(400)) {
                activationSocket.write(QByteArrayLiteral("activate"));
                activationSocket.flush();
                activationSocket.waitForBytesWritten(400);
                activationSocket.disconnectFromServer();
                activationSent = true;
            } else if (attempt < 4) {
                QThread::msleep(50);
            }
        }
        Logger::instance().info(activationSent
            ? QStringLiteral("Another MeloBox instance is already running; activation sent")
            : QStringLiteral("Another MeloBox instance is already running; activation server not ready"));
        return 0;
    }

    AppBootstrap bootstrap;
    if (!bootstrap.initialize()) {
        Logger::instance().error(QStringLiteral("Application bootstrap failed"));
        return -1;
    }

    return app.exec();
}
