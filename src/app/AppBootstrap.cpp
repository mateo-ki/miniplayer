#include "app/AppBootstrap.h"

#include <QUrl>
#include <QQmlContext>
#include <QtGlobal>

namespace {
constexpr char16_t kMainModuleUri[] = u"MiniPlayer";
constexpr char16_t kMainTypeName[] = u"Main";
}

bool AppBootstrap::initialize() {
    configureEngine();
    registerQmlTypes();
    exposeContextProperties();
    return loadMainWindow();
}

void AppBootstrap::configureEngine() {
    m_engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
}

void AppBootstrap::registerQmlTypes() {
    // Future controller and singleton registrations belong here.
}

void AppBootstrap::exposeContextProperties() {
    m_engine.rootContext()->setContextProperty("playerController", &m_playerController);
}

bool AppBootstrap::loadMainWindow() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    m_engine.loadFromModule(kMainModuleUri, kMainTypeName);
#else
    m_engine.load(QUrl(QStringLiteral("qrc:/qt/qml/MiniPlayer/Main.qml")));
#endif

    return !m_engine.rootObjects().isEmpty();
}
