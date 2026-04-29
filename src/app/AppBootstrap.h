#pragma once

#include <QtQml/QQmlApplicationEngine>

class AppBootstrap {
public:
    void initialize();

private:
    QQmlApplicationEngine m_engine;
};
