#pragma once

#include <QtQml/QQmlApplicationEngine>

class AppBootstrap {
public:
    bool initialize();

private:
    void configureEngine();
    void registerQmlTypes();
    bool loadMainWindow();

    QQmlApplicationEngine m_engine;
};
