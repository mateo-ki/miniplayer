#include "app/AppBootstrap.h"

#include <QUrl>
#include <QQmlContext>
#include <QtGlobal>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QFileInfo>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QMetaObject>
#include <QByteArray>
#include <QCursor>
#include "common/constant.h"
#include "infrastructure/Logger.h"
#include "render/VideoFrameBridge.h"
#include "render/ThumbnailImageProvider.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

extern "C" {
#include <libavutil/log.h>
}

namespace {
constexpr char16_t kMainModuleUri[] = u"MiniPlayer";
constexpr char16_t kMainTypeName[] = u"Main";

QIcon appIcon() {
    QIcon icon(QStringLiteral(":/qt/qml/MiniPlayer/ui/qml/assets/appicon.png"));
    if (!icon.isNull()) {
        return icon;
    }

    const QString diskIconPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/appicon.ico");
    if (QFileInfo::exists(diskIconPath)) {
        icon = QIcon(diskIconPath);
        if (!icon.isNull()) {
            return icon;
        }
    }

    return QIcon(QStringLiteral(":/qt/qml/MiniPlayer/ui/qml/assets/appicon.png"));
}

void ensureWindowOnScreen(QQuickWindow *window) {
    if (!window) return;

    const QRect windowRect(window->x(), window->y(), window->width(), window->height());
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen && screen->availableGeometry().intersects(windowRect)) {
            return;
        }
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    const QRect available = screen->availableGeometry();
    const int width = qMin(window->width(), available.width());
    const int height = qMin(window->height(), available.height());
    if (width != window->width() || height != window->height()) {
        window->resize(width, height);
    }
    window->setPosition(
        available.x() + (available.width() - width) / 2,
        available.y() + (available.height() - height) / 2);
}
}

AppBootstrap::AppBootstrap(QObject *parent)
    : QObject(parent) {
}

bool AppBootstrap::initialize() {
    Logger::instance().info(QStringLiteral("AppBootstrap initialize begin"));
    av_log_set_level(AV_LOG_ERROR);
    QCoreApplication::instance()->installEventFilter(this);
    QCoreApplication::instance()->installNativeEventFilter(this);
    configureEngine();
    registerQmlTypes();
    exposeContextProperties();
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [this]() {
        m_playerController.stop();
    });
    if (!loadMainWindow()) {
        Logger::instance().error(QStringLiteral("Main window load failed: no root QML objects"));
        return false;
    }
    if (auto *win = mainWindow()) {
        Logger::instance().info(QStringLiteral("Main window loaded: %1x%2 visible=%3")
            .arg(win->width())
            .arg(win->height())
            .arg(win->isVisible()));
        win->setIcon(appIcon());
        win->showNormal();
        ensureWindowOnScreen(win);
        win->setVisible(true);
        win->raise();
        win->requestActivate();
        Logger::instance().info(QStringLiteral("Main window show requested: x=%1 y=%2 w=%3 h=%4 visible=%5 active=%6")
            .arg(win->x())
            .arg(win->y())
            .arg(win->width())
            .arg(win->height())
            .arg(win->isVisible())
            .arg(win->isActive()));
        QTimer::singleShot(250, win, [win]() {
            win->showNormal();
            ensureWindowOnScreen(win);
            win->setVisible(true);
            win->raise();
            win->requestActivate();
            Logger::instance().info(QStringLiteral("Main window delayed show requested: x=%1 y=%2 w=%3 h=%4 visible=%5 active=%6")
                .arg(win->x())
                .arg(win->y())
                .arg(win->width())
                .arg(win->height())
                .arg(win->isVisible())
                .arg(win->isActive()));
        });
        m_playerController.setMpvWindowId(static_cast<qint64>(win->winId()));
    } else {
        Logger::instance().error(QStringLiteral("Main window load failed: root object is not QQuickWindow"));
        return false;
    }
    setupSystemTray();

    // Connect thumbnail updates to image provider (before QML sees the change)
    QObject::connect(&m_playerController, &PlayerController::thumbnailReadyForProvider,
        [this](const QString &key, const QImage &image) {
            m_thumbnailProvider->update(key, image);
        });

    return true;
}

bool AppBootstrap::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::WindowActivate
            || (event->type() == QEvent::Enter && watched == mainWindow())) {
        if (auto *win = mainWindow()) {
            win->setCursor(QCursor(Qt::ArrowCursor));
        }
        m_playerController.resetMouseCursor();
    }

    if (event->type() != QEvent::KeyPress) {
        return QObject::eventFilter(watched, event);
    }

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (!shortcutContextActive() || shouldIgnoreShortcut()) {
        return QObject::eventFilter(watched, event);
    }

    if (keyEvent->modifiers() != Qt::NoModifier) {
        return QObject::eventFilter(watched, event);
    }

    if (handleShortcutKey(keyEvent->key(), keyEvent->isAutoRepeat())) {
        keyEvent->accept();
        return true;
    }

    return QObject::eventFilter(watched, event);
}

bool AppBootstrap::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
    Q_UNUSED(eventType)
    Q_UNUSED(result)

#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    if (!msg || (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)) {
        return false;
    }

    if (!shortcutContextActive() || shouldIgnoreShortcut()) {
        return false;
    }

    const bool hasModifier =
        (GetKeyState(VK_CONTROL) & 0x8000) ||
        (GetKeyState(VK_MENU) & 0x8000) ||
        (GetKeyState(VK_SHIFT) & 0x8000);
    if (hasModifier) {
        return false;
    }

    int qtKey = 0;
    switch (msg->wParam) {
    case VK_SPACE:
        qtKey = Qt::Key_Space;
        break;
    case VK_LEFT:
        qtKey = Qt::Key_Left;
        break;
    case VK_RIGHT:
        qtKey = Qt::Key_Right;
        break;
    case VK_OEM_4:
        qtKey = Qt::Key_BracketLeft;
        break;
    case VK_OEM_6:
        qtKey = Qt::Key_BracketRight;
        break;
    default:
        return false;
    }

    const bool autoRepeat = (msg->lParam & (1 << 30)) != 0;
    if (handleShortcutKey(qtKey, autoRepeat)) {
        return true;
    }
#endif

    return false;
}

void AppBootstrap::configureEngine() {
    QGuiApplication::setWindowIcon(appIcon());
    initQtRuntimeEnv(m_engine);
    m_engine.addImportPath(QStringLiteral("qrc:/qt/qml"));

    // Register thumbnail image provider
    m_thumbnailProvider = new ThumbnailImageProvider;
    m_engine.addImageProvider("thumbnail", m_thumbnailProvider);
}

void AppBootstrap::registerQmlTypes() {
    qmlRegisterType<VideoFrameBridge>("MiniPlayer", 1, 0, "VideoFrame");
}

void AppBootstrap::exposeContextProperties() {
    m_engine.rootContext()->setContextProperty("playerController", &m_playerController);
}

bool AppBootstrap::loadMainWindow() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    Logger::instance().info(QStringLiteral("Loading QML module %1.%2")
        .arg(QString::fromUtf16(kMainModuleUri))
        .arg(QString::fromUtf16(kMainTypeName)));
    m_engine.loadFromModule(kMainModuleUri, kMainTypeName);
#else
    Logger::instance().info(QStringLiteral("Loading QML from qrc:/qt/qml/MiniPlayer/Main.qml"));
    m_engine.load(QUrl(QStringLiteral("qrc:/qt/qml/MiniPlayer/Main.qml")));
#endif

    Logger::instance().info(QStringLiteral("QML root object count=%1").arg(m_engine.rootObjects().size()));
    return !m_engine.rootObjects().isEmpty();
}

void AppBootstrap::setupSystemTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        Logger::instance().warn(QStringLiteral("System tray is not available"));
        if (auto *win = mainWindow()) {
            win->setProperty("trayAvailable", false);
        }
        return;
    }

    m_trayMenu = new QMenu;
    m_showHideAction = m_trayMenu->addAction("Hide MeloBox", [this]() {
        toggleMainWindow();
    });
    m_playPauseAction = m_trayMenu->addAction("Play", [this]() {
        if (m_playerController.isPlaying()) m_playerController.pause();
        else m_playerController.play();
    });
    m_stopAction = m_trayMenu->addAction("Stop", [this]() {
        m_playerController.stop();
    });
    m_trayMenu->addSeparator();
    m_previousAction = m_trayMenu->addAction("Previous", [this]() {
        m_playerController.playPrevious();
    });
    m_nextAction = m_trayMenu->addAction("Next", [this]() {
        m_playerController.playNext();
    });
    m_muteAction = m_trayMenu->addAction("Mute", [this]() {
        m_playerController.setMuted(!m_playerController.muted());
    });
    m_trayMenu->addSeparator();
    m_quitAction = m_trayMenu->addAction("Quit", [this]() {
        quitApplication();
    });

    m_trayIcon = new QSystemTrayIcon(QCoreApplication::instance());
    m_trayIcon->setIcon(appIcon());
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("MeloBox");
    m_trayIcon->show();
    Logger::instance().info(QStringLiteral("System tray initialized"));

    if (auto *win = mainWindow()) {
        win->setProperty("trayAvailable", true);
    }

    QObject::connect(&m_playerController, &PlayerController::playbackStateChanged, [this]() {
        updateTrayActions();
    });
    QObject::connect(&m_playerController, &PlayerController::mutedChanged, [this]() {
        updateTrayActions();
    });
    QObject::connect(&m_playerController, &PlayerController::currentFileChanged, [this]() {
        updateTrayActions();
    });
    QObject::connect(m_playerController.playlistModel(), &PlaylistModel::countChanged, [this]() {
        updateTrayActions();
    });

    QObject::connect(m_trayIcon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick) return;
        toggleMainWindow();
    });

    updateTrayActions();
}

QQuickWindow *AppBootstrap::mainWindow() const {
    if (m_engine.rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QQuickWindow *>(m_engine.rootObjects().first());
}

void AppBootstrap::showMainWindow() {
    auto *win = mainWindow();
    if (!win) return;
    win->showNormal();
    ensureWindowOnScreen(win);
    win->setVisible(true);
    win->raise();
    win->requestActivate();
    updateTrayActions();
}

void AppBootstrap::hideMainWindow() {
    auto *win = mainWindow();
    if (!win) return;
    win->setVisible(false);
    updateTrayActions();
}

void AppBootstrap::toggleMainWindow() {
    auto *win = mainWindow();
    if (!win) return;
    if (win->isVisible()) {
        hideMainWindow();
    } else {
        showMainWindow();
    }
}

void AppBootstrap::updateTrayActions() {
    if (m_showHideAction) {
        const bool visible = mainWindow() && mainWindow()->isVisible();
        m_showHideAction->setText(visible ? "Hide MeloBox" : "Show MeloBox");
    }
    if (m_playPauseAction) {
        m_playPauseAction->setText(m_playerController.isPlaying() ? "Pause" : "Play");
    }
    if (m_stopAction) {
        m_stopAction->setEnabled(!m_playerController.currentFile().isEmpty());
    }
    if (m_previousAction) {
        m_previousAction->setEnabled(m_playerController.playlistModel()->count() > 1);
    }
    if (m_nextAction) {
        m_nextAction->setEnabled(m_playerController.playlistModel()->count() > 1);
    }
    if (m_muteAction) {
        m_muteAction->setText(m_playerController.muted() ? "Unmute" : "Mute");
    }
}

void AppBootstrap::quitApplication() {
    if (auto *win = mainWindow()) {
        win->setProperty("forceQuit", true);
    }
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    m_playerController.stop();
    QCoreApplication::quit();
}

bool AppBootstrap::shouldIgnoreShortcut() const {
    QObject *focusObject = QGuiApplication::focusObject();
    if (!focusObject) return false;

    QString className = QString::fromLatin1(focusObject->metaObject()->className());
    while (focusObject) {
        className = QString::fromLatin1(focusObject->metaObject()->className());
        if (className.contains(QStringLiteral("TextInput"), Qt::CaseInsensitive) ||
            className.contains(QStringLiteral("TextEdit"), Qt::CaseInsensitive) ||
            className.contains(QStringLiteral("TextArea"), Qt::CaseInsensitive) ||
            className.contains(QStringLiteral("LineEdit"), Qt::CaseInsensitive)) {
            return true;
        }
        focusObject = focusObject->parent();
    }

    return false;
}

bool AppBootstrap::shortcutContextActive() const {
    auto *win = mainWindow();
    if (!win || !win->isVisible()) return false;
    if (win->isActive()) return true;

#ifdef Q_OS_WIN
    HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow) return false;

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    if (foregroundProcessId == GetCurrentProcessId()) {
        return true;
    }

    HWND rootWindow = GetAncestor(foregroundWindow, GA_ROOT);
    return reinterpret_cast<HWND>(win->winId()) == foregroundWindow
        || reinterpret_cast<HWND>(win->winId()) == rootWindow;
#else
    return false;
#endif
}

bool AppBootstrap::handleShortcutKey(int key, bool autoRepeat) {
    switch (key) {
    case Qt::Key_Space:
        if (!autoRepeat) {
            Logger::instance().info("Keyboard shortcut: Space play/pause");
            if (m_playerController.isPlaying()) {
                m_playerController.pause();
            } else {
                m_playerController.play();
            }
        }
        return true;
    case Qt::Key_Left:
        Logger::instance().info("Keyboard shortcut: Left");
        seekRelative(-5000);
        return true;
    case Qt::Key_Right:
        Logger::instance().info("Keyboard shortcut: Right");
        seekRelative(5000);
        return true;
    case Qt::Key_BracketLeft:
        adjustVolume(-0.05f);
        return true;
    case Qt::Key_BracketRight:
        adjustVolume(0.05f);
        return true;
    default:
        return false;
    }
}

void AppBootstrap::seekRelative(qint64 deltaMs) {
    const qint64 duration = m_playerController.durationMs();
    if (duration <= 0) return;

    const qint64 target = qBound<qint64>(0, m_playerController.positionMs() + deltaMs, duration);
    Logger::instance().info(QStringLiteral("Keyboard shortcut: seek %1 ms to %2")
        .arg(deltaMs)
        .arg(target));
    m_playerController.seek(target);
}

void AppBootstrap::adjustVolume(float delta) {
    const float target = qBound(0.0f, m_playerController.volume() + delta, 1.0f);
    Logger::instance().info(QStringLiteral("Keyboard shortcut: volume %1")
        .arg(target, 0, 'f', 2));
    m_playerController.setVolume(target);
}
