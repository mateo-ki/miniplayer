#include "app/AppBootstrap.h"

#include <QCoreApplication>
#include <QUrl>

void AppBootstrap::initialize() {
    m_engine.load(QUrl(QStringLiteral("qrc:/qt/qml/MiniPlayer/ui/qml/Main.qml")));
    if (m_engine.rootObjects().isEmpty()) {
        QCoreApplication::exit(-1);
    }
}
