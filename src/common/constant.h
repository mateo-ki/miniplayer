//
// Created by loukas on 2026/3/21.
//

#ifndef IFFMPEG_CONSTANT_H
#define IFFMPEG_CONSTANT_H

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QString>
#include <QtQml/QQmlApplicationEngine>

inline void initQtRuntimeEnv(QQmlApplicationEngine &engine)
{
    qputenv("QT_IM_MODULE", "qtvirtualkeyboard");
    qputenv("QT_VIRTUALKEYBOARD_DESKTOP_DISABLE", "0");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    qputenv("QSG_RHI_BACKEND", "d3d11");

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString deployedQmlPath = QDir(appDir).filePath(QStringLiteral("qml"));
    const QString deployedPluginPath = QDir(appDir).filePath(QStringLiteral("plugins"));

    if (QDir(deployedQmlPath).exists()) {
        engine.addImportPath(deployedQmlPath);
    }

    QCoreApplication::addLibraryPath(appDir);
    if (QDir(deployedPluginPath).exists()) {
        QCoreApplication::addLibraryPath(deployedPluginPath);
    }

    qDebug() << "Qt runtime env initialized:"
             << "appDir=" << appDir
             << "qml=" << deployedQmlPath
             << "plugins=" << deployedPluginPath;
}

#endif // IFFMPEG_CONSTANT_H
