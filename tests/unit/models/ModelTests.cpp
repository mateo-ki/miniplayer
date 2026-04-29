#include <QtTest>

#include "infrastructure/Logger.h"
#include "models/MediaInfoModel.h"
#include "models/RuntimeLogModel.h"

class RuntimeLogModelTests : public QObject {
    Q_OBJECT

private slots:
    void appendsVisibleLogEntries();
    void clearRemovesAllEntries();
};

class MediaInfoModelTests : public QObject {
    Q_OBJECT

private slots:
    void replaceAllUpdatesRows();
};

class LoggerTests : public QObject {
    Q_OBJECT

private slots:
    void emitsThroughConfiguredSink();
};

void RuntimeLogModelTests::appendsVisibleLogEntries() {
    RuntimeLogModel model;

    model.append("info", "ffmpeg initialized");

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex first = model.index(0, 0);
    QCOMPARE(model.data(first, RuntimeLogModel::LevelRole).toString(), QString("info"));
    QCOMPARE(model.data(first, RuntimeLogModel::MessageRole).toString(), QString("ffmpeg initialized"));
}

void RuntimeLogModelTests::clearRemovesAllEntries() {
    RuntimeLogModel model;
    model.append("warn", "temporary entry");

    model.clear();

    QCOMPARE(model.rowCount(), 0);
}

void MediaInfoModelTests::replaceAllUpdatesRows() {
    MediaInfoModel model;
    const QVector<MediaInfoItem> items = {
        { "Container", "MPEG-4" },
        { "Duration", "00:03:42" }
    };

    model.replaceAll(items);

    QCOMPARE(model.rowCount(), 2);
    const QModelIndex first = model.index(0, 0);
    QCOMPARE(model.data(first, MediaInfoModel::KeyRole).toString(), QString("Container"));
    QCOMPARE(model.data(first, MediaInfoModel::ValueRole).toString(), QString("MPEG-4"));
}

void LoggerTests::emitsThroughConfiguredSink() {
    QString emittedLevel;
    QString emittedMessage;

    Logger::instance().setSink([&](const QString &level, const QString &message) {
        emittedLevel = level;
        emittedMessage = message;
    });

    Logger::instance().error("decode failed");

    QCOMPARE(emittedLevel, QString("error"));
    QCOMPARE(emittedMessage, QString("decode failed"));
}

int main(int argc, char **argv) {
    int status = 0;

    {
        RuntimeLogModelTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        MediaInfoModelTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        LoggerTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }

    return status;
}

#include "ModelTests.moc"
