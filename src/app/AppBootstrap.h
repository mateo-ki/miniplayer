#pragma once

#include "controller/PlayerController.h"
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>
#include <QtQml/QQmlApplicationEngine>

class QSystemTrayIcon;
class QMenu;
class QAction;
class QLocalServer;
class QQuickWindow;
class ThumbnailImageProvider;

class AppBootstrap : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit AppBootstrap(QObject *parent = nullptr);
    bool initialize();
    static QString instanceServerName();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void configureEngine();
    void registerQmlTypes();
    void exposeContextProperties();
    bool loadMainWindow();
    bool setupInstanceServer();
    void setupSystemTray();
    QQuickWindow *mainWindow() const;
    void showMainWindow();
    void hideMainWindow();
    void toggleMainWindow();
    void updateTrayActions();
    void quitApplication();
    bool shouldIgnoreShortcut() const;
    bool shortcutContextActive() const;
    bool handleShortcutKey(int key, bool autoRepeat);
    void seekRelative(qint64 deltaMs);
    void adjustVolume(float delta);

    QQmlApplicationEngine m_engine;
    PlayerController m_playerController;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_showHideAction = nullptr;
    QAction *m_playPauseAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_previousAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_muteAction = nullptr;
    QAction *m_quitAction = nullptr;
    ThumbnailImageProvider *m_thumbnailProvider = nullptr;
    QLocalServer *m_instanceServer = nullptr;
};
