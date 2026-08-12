#include <QtTest>

#include <QGuiApplication>

#include "controller/PlayerController.h"
#include "infrastructure/Logger.h"
#include "models/ApiSiteModel.h"
#include "models/MediaInfoModel.h"
#include "models/RuntimeLogModel.h"
#include "models/VideoSearchModel.h"

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

class PlayerControllerTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void exposesInitialIdleState();
    void playPauseStopUpdateState();
    void vodSelectionPublishesTargetImmediately();
};

class ApiSiteModelTests : public QObject {
    Q_OBJECT

private slots:
    void matchesFilterByNameOrUrl();
    void deduplicateByNormalizedUrlKeepsFirstSite();
    void defaultsSiteTypeToVideo();
    void resolvesVideoAndImageSitesSeparately();
};

class VideoSearchModelTests : public QObject {
    Q_OBJECT

private slots:
    void exposesEmptyInitialState();
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

void PlayerControllerTests::initTestCase() {
    // No-op: don't clear user settings.
}

void PlayerControllerTests::exposesInitialIdleState() {
    PlayerController controller;

    QCOMPARE(controller.isPlaying(), false);
    QCOMPARE(controller.isPaused(), false);
    QCOMPARE(controller.durationMs(), 0);
    QCOMPARE(controller.positionMs(), 0);
    QCOMPARE(controller.muted(), false);
    QCOMPARE(controller.currentFile(), QString(""));
    QVERIFY(controller.mediaInfoModel() != nullptr);
    QVERIFY(controller.runtimeLogModel() != nullptr);
}

void PlayerControllerTests::playPauseStopUpdateState() {
    PlayerController controller;

    controller.play();
    QCOMPARE(controller.isPlaying(), true);
    QCOMPARE(controller.isPaused(), false);

    controller.pause();
    QCOMPARE(controller.isPlaying(), true);
    QCOMPARE(controller.isPaused(), true);

    controller.stop();
    QCOMPARE(controller.isPlaying(), false);
    QCOMPARE(controller.isPaused(), false);
    QCOMPARE(controller.positionMs(), 0);
}

void PlayerControllerTests::vodSelectionPublishesTargetImmediately() {
    PlayerController controller;
    const QString url = QStringLiteral("https://media.example.com/episode.m3u8");

    controller.playVodUrl(url);

    QCOMPARE(controller.currentFile(), url);
    QCOMPARE(controller.loading(), true);
}

void ApiSiteModelTests::matchesFilterByNameOrUrl() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    model.add("Alpha Site", "https://alpha.example.com/provide/vod");
    model.add("Beta Site", "https://beta.example.com/api");

    QVERIFY(model.matchesFilter(0, "alpha"));
    QVERIFY(model.matchesFilter(0, "provide/vod"));
    QVERIFY(!model.matchesFilter(1, "alpha"));
    QVERIFY(model.matchesFilter(1, ""));
}

void ApiSiteModelTests::deduplicateByNormalizedUrlKeepsFirstSite() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    model.add("First", "https://example.com/api/");
    model.add("Duplicate", "HTTPS://EXAMPLE.COM/API");
    model.add("Other", "https://other.example.com/api");

    model.setCurrentIndex(1);
    const QString message = model.deduplicateByUrl();

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.nameAt(0), QString("First"));
    QCOMPARE(model.baseUrlAt(0), QString("https://example.com/api/"));
    QCOMPARE(model.nameAt(1), QString("Other"));
    QCOMPARE(model.currentIndex(), 0);
    QVERIFY(message.contains("1"));
}

void ApiSiteModelTests::defaultsSiteTypeToVideo() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);

    model.add("Legacy", "https://video.example.com/api");

    QCOMPARE(model.typeAt(0), QString("video"));
    QCOMPARE(model.currentVideoBaseUrl(), QString("https://video.example.com/api"));
}

void ApiSiteModelTests::resolvesVideoAndImageSitesSeparately() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    model.add("Video", "https://video.example.com/api", "video");
    model.add("Image", "https://image.example.com/json", "image");

    model.setCurrentIndex(1);

    QCOMPARE(model.currentBaseUrl(), QString("https://image.example.com/json"));
    QCOMPARE(model.currentVideoBaseUrl(), QString("https://video.example.com/api"));
    QCOMPARE(model.imageBaseUrl(), QString("https://image.example.com/json"));
}

void VideoSearchModelTests::exposesEmptyInitialState() {
    VideoSearchModel model;

    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.currentPage(), 1);
    QCOMPARE(model.totalPages(), 1);
    QCOMPARE(model.errorMessage(), QString());
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication app(argc, argv);
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
    {
        PlayerControllerTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        ApiSiteModelTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        VideoSearchModelTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }

    return status;
}

#include "ModelTests.moc"
