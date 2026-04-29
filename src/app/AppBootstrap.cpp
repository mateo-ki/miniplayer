#include "app/AppBootstrap.h"

#include <QUrl>
#include <QtGlobal>

namespace {
constexpr auto kMainModuleUri = "MiniPlayer";
constexpr auto kMainTypeName = "Main";
}

bool AppBootstrap::initialize() {
    configureEngine();
    registerQmlTypes();
    return loadMainWindow();
}

void AppBootstrap::configureEngine() {
    m_engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
}

void AppBootstrap::registerQmlTypes() {
    // Future controller and singleton registrations belong here.
}

bool AppBootstrap::loadMainWindow() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    m_engine.loadFromModule(QStringLiteral(kMainModuleUri), QStringLiteral(kMainTypeName));
#else
    m_engine.load(QUrl(QStringLiteral("qrc:/qt/qml/MiniPlayer/Main.qml")));
#endif

    return !m_engine.rootObjects().isEmpty();
}
