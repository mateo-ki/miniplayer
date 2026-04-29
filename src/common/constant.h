//
// Created by loukas on 2026/3/21.
//

#ifndef IFFMPEG_CONSTANT_H
#define IFFMPEG_CONSTANT_H
#include <qfileinfo.h>
#include <qstring.h>
#include <QProcess>
#include <QDir>
#include <iostream>
#include <qcoreapplication.h>
#include <qfileinfo.h>
#include <qstring.h>
#include <QProcess>
#include <QDir>
#include <QtQml/QQmlApplicationEngine>


QString getVcpkgPath()
{
    QProcess process;
    QString program;
    QStringList arguments;

    program = "cmd";

    arguments << "/c"<<" where "<<"vcpkg";
    process.start(program,arguments);

    if(!process.waitForFinished(200)) {
        qDebug()<<"执行失败";
        exit(0);
    }
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();


    if (output.isEmpty()) {
        qDebug()<<"where vcpkg输出为空";
        exit(0);
    }

    QString exePath = output.split("\n").first().trimmed();

    exePath = exePath.remove("\t");

    if (!QFileInfo::exists(exePath)) {
        qDebug()<<"vcpkg.exe 不存在!";
        exit(0);
    }
    qDebug() << "[Info] 找到 vcpkg.exe:" << exePath;

    // --- 核心逻辑：从 exe 路径推导根目录 ---
    QFileInfo fi(exePath);
    QString dirPath = fi.absolutePath(); // 获取 exe 所在目录

    QString rootPath;
    // vcpkg 结构通常是 <root>/vcpkg.exe 或者 <root>/bin/vcpkg.exe
    if (dirPath.endsWith("bin", Qt::CaseInsensitive)) {
        // 如果在 bin 目录下，根目录是上一级
        rootPath = QDir(dirPath).absolutePath();
    } else {
        // 如果直接在根目录下
        rootPath = dirPath;
    }

    qDebug() << "[Info] 推断的 vcpkg 根目录:" << rootPath;
    return rootPath;

};

void  initVcpkgRuntimeEnv(QQmlApplicationEngine& engine)
{

    QString vcpkgRoot = getVcpkgPath();

    if (vcpkgRoot.isEmpty()) {
        qCritical() << "无法定位 vcpkg 根目录";
        return;
    }

    QString baseInstalled = QDir(vcpkgRoot).filePath("installed/x64-windows");

    QString binPath = QDir(baseInstalled).filePath("debug/bin");
    QString pluginPath = QDir(baseInstalled).filePath("debug/Qt6/plugins");
    QString qmlPath = QDir(baseInstalled).filePath("debug/Qt6/qml");
    qputenv("PATH", QByteArray(binPath.toLocal8Bit() + ";") + qgetenv("PATH"));
    qputenv("QT_IM_MODULE", "qtvirtualkeyboard");
    qputenv("QT_VIRTUALKEYBOARD_DESKTOP_DISABLE", "0");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QCoreApplication::addLibraryPath(pluginPath);
    QCoreApplication::addLibraryPath(pluginPath + "/platforminputcontexts");
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& msg)
     {
         std::cout << msg.toStdString() << std::endl;
     });
    engine.addImportPath(qmlPath);
    qDebug() << "Vcpkg env initialized";

};

#endif //IFFMPEG_CONSTANT_H