#pragma once

#include "controller/PlayerController.h"
#include <QtQml/QQmlApplicationEngine>

class AppBootstrap {
public:
    bool initialize();

private:
    void configureEngine();
    void registerQmlTypes();
    void exposeContextProperties();
    bool loadMainWindow();

    QQmlApplicationEngine m_engine;
    PlayerController m_playerController;
};
