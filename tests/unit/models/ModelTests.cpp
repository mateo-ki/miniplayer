#include <QtTest>

#include <QGuiApplication>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

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
    void exposesIdleRemoteSiteLoadingState();
    void loadsRemoteSitesWhenEnabled();
    void movesSitesAndPreservesSelectedSite();
    void movesSitesToInsertionSlots_data();
    void movesSitesToInsertionSlots();
    void keepsInsertionSlotsInsidePremiumGroup();
    void removesSelectedSitesAndPreservesRemainingCurrentSite();
    void premiumFlagMovesWithSite();
    void persistsOrderAndPremiumFlag();
    void sharesAndImportsPremiumFlag();
    void importsJsonVideoSitesWithDeduplication();
    void rejectsInvalidJsonVideoSites();
    void loadsJsonVideoSitesWhenEnabled();
};

class VideoSearchModelTests : public QObject {
    Q_OBJECT

private slots:
    void exposesEmptyInitialState();
    void parsesParentCategoryIds();
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

void ApiSiteModelTests::exposesIdleRemoteSiteLoadingState() {
    ApiSiteModel model;

    QCOMPARE(model.remoteSitesLoading(), false);
}

void ApiSiteModelTests::loadsRemoteSitesWhenEnabled() {
    if (qEnvironmentVariableIntValue("MELOBOX_RUN_NETWORK_TESTS") != 1)
        QSKIP("Set MELOBOX_RUN_NETWORK_TESTS=1 to run the remote site test");

    ApiSiteModel model;
    QSignalSpy finishedSpy(&model, &ApiSiteModel::remoteSitesLoadFinished);

    model.loadRemoteSites();

    QCOMPARE(model.remoteSitesLoading(), true);
    QVERIFY2(finishedSpy.wait(20000), "remote site loading did not finish within 20 seconds");
    QCOMPARE(model.remoteSitesLoading(), false);
    QCOMPARE(finishedSpy.size(), 1);
    const QString message = finishedSpy.takeFirst().at(0).toString();
    QVERIFY2(message.startsWith(QStringLiteral("Import complete:")), qPrintable(message));
    QVERIFY(model.count() > 0);
}

void ApiSiteModelTests::movesSitesAndPreservesSelectedSite() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    model.add("Alpha", "https://alpha.example.com");
    model.add("Beta", "https://beta.example.com");
    model.add("Gamma", "https://gamma.example.com");
    model.setCurrentIndex(1);

    QVERIFY(model.moveSite(0, 2));
    QCOMPARE(model.nameAt(0), QString("Beta"));
    QCOMPARE(model.nameAt(1), QString("Gamma"));
    QCOMPARE(model.nameAt(2), QString("Alpha"));
    QCOMPARE(model.currentIndex(), 0);
    QCOMPARE(model.currentName(), QString("Beta"));

    QVERIFY(model.moveSite(0, 2));
    QCOMPARE(model.currentIndex(), 2);
    QCOMPARE(model.currentName(), QString("Beta"));
}

void ApiSiteModelTests::movesSitesToInsertionSlots_data() {
    QTest::addColumn<int>("from");
    QTest::addColumn<int>("slot");
    QTest::addColumn<bool>("moved");
    QTest::addColumn<QStringList>("expected");

    const QStringList original = {"A", "B", "C", "D"};
    QTest::newRow("first-before-third") << 0 << 2 << true << QStringList{"B", "A", "C", "D"};
    QTest::newRow("first-after-third") << 0 << 3 << true << QStringList{"B", "C", "A", "D"};
    QTest::newRow("last-before-second") << 3 << 1 << true << QStringList{"A", "D", "B", "C"};
    QTest::newRow("last-after-second") << 3 << 2 << true << QStringList{"A", "B", "D", "C"};
    QTest::newRow("second-to-top") << 1 << 0 << true << QStringList{"B", "A", "C", "D"};
    QTest::newRow("second-to-end") << 1 << 4 << true << QStringList{"A", "C", "D", "B"};
    QTest::newRow("adjacent-up") << 2 << 1 << true << QStringList{"A", "C", "B", "D"};
    QTest::newRow("adjacent-down") << 1 << 3 << true << QStringList{"A", "C", "B", "D"};
    QTest::newRow("own-leading-slot") << 1 << 1 << false << original;
    QTest::newRow("own-trailing-slot") << 1 << 2 << false << original;
}

void ApiSiteModelTests::movesSitesToInsertionSlots() {
    QFETCH(int, from);
    QFETCH(int, slot);
    QFETCH(bool, moved);
    QFETCH(QStringList, expected);

    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    for (const QString &name : {"A", "B", "C", "D"})
        model.add(name, QStringLiteral("https://%1.example.com").arg(name.toLower()));

    QCOMPARE(model.moveSiteToSlot(from, slot), moved);
    QStringList actual;
    for (int index = 0; index < model.count(); ++index)
        actual.append(model.nameAt(index));
    QCOMPARE(actual, expected);
}

void ApiSiteModelTests::keepsInsertionSlotsInsidePremiumGroup() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    model.add("P1", "https://p1.example.com", "video", true);
    model.add("P2", "https://p2.example.com", "video", true);
    model.add("N1", "https://n1.example.com");
    model.add("N2", "https://n2.example.com");

    QVERIFY(!model.moveSiteToSlot(1, 4));
    QVERIFY(!model.moveSiteToSlot(2, 0));
    QVERIFY(model.moveSiteToSlot(1, 0));
    QVERIFY(model.moveSiteToSlot(2, 4));

    const QStringList expected = {"P2", "P1", "N2", "N1"};
    QStringList actual;
    for (int index = 0; index < model.count(); ++index)
        actual.append(model.nameAt(index));
    QCOMPARE(actual, expected);
}

void ApiSiteModelTests::removesSelectedSitesAndPreservesRemainingCurrentSite() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    model.add("Alpha", "https://alpha.example.com");
    model.add("Beta", "https://beta.example.com");
    model.add("Gamma", "https://gamma.example.com");
    model.setCurrentIndex(1);
    model.setShareSelected(0, true);
    model.setShareSelected(2, true);

    const QString message = model.removeSelectedSites();

    QCOMPARE(message, QStringLiteral("已删除 2 个选中站点"));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.currentIndex(), 0);
    QCOMPARE(model.currentName(), QString("Beta"));
    QCOMPARE(model.shareSelectedAt(0), false);

    model.setShareSelected(0, true);
    QCOMPARE(model.removeSelectedSites(), QStringLiteral("删除失败：至少需要保留一个站点"));
    QCOMPARE(model.count(), 1);

    model.setShareSelected(0, false);
    model.add("Delta", "https://delta.example.com");
    model.add("Epsilon", "https://epsilon.example.com");
    model.setCurrentIndex(1);
    model.setShareSelected(1, true);

    QCOMPARE(model.removeSelectedSites(), QStringLiteral("已删除 1 个选中站点"));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.currentIndex(), 1);
    QCOMPARE(model.currentName(), QString("Epsilon"));
}

void ApiSiteModelTests::premiumFlagMovesWithSite() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    model.add("Standard", "https://standard.example.com");
    model.add("Premium", "https://premium.example.com", "video", true);

    QCOMPARE(model.nameAt(0), QString("Premium"));
    QCOMPARE(model.premiumAt(0), true);

    QVERIFY(!model.moveSite(1, 0));
    QCOMPARE(model.nameAt(0), QString("Premium"));

    model.togglePremium(0);
    QCOMPARE(model.premiumAt(0), false);

    model.setPremium(1, true);
    QCOMPARE(model.nameAt(0), QString("Standard"));
    QCOMPARE(model.premiumAt(0), true);
    QCOMPARE(model.nameAt(1), QString("Premium"));
}

void ApiSiteModelTests::persistsOrderAndPremiumFlag() {
    {
        ApiSiteModel model;
        while (model.count() > 0)
            model.removeAt(0);
        model.add("First", "https://first.example.com");
        model.add("Second", "https://second.example.com", "video", true);
    }

    ApiSiteModel restored;
    QCOMPARE(restored.nameAt(0), QString("Second"));
    QCOMPARE(restored.premiumAt(0), true);
    QCOMPARE(restored.nameAt(1), QString("First"));
}

void ApiSiteModelTests::sharesAndImportsPremiumFlag() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    model.add("Premium", "https://premium-share.example.com", "video", true);
    model.setShareSelected(0, true);

    const QString shareMessage = model.shareSelectedToClipboard();
    QVERIFY2(shareMessage.startsWith(QStringLiteral("Encrypted ")), qPrintable(shareMessage));

    while (model.count() > 0)
        model.removeAt(0);
    const QString importMessage = model.importSitesFromClipboard();

    QVERIFY2(importMessage.startsWith(QStringLiteral("Import complete:")), qPrintable(importMessage));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.nameAt(0), QString("Premium"));
    QCOMPARE(model.premiumAt(0), true);
}

void ApiSiteModelTests::importsJsonVideoSitesWithDeduplication() {
    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    model.add("Existing", "https://existing.example.com/api", "image", true);
    model.add("Duplicate existing", "HTTPS://EXISTING.EXAMPLE.COM/API/");

    const QByteArray json = R"json([
        {"name":"Updated existing","api":"https://existing.example.com/api"},
        {"name":"New video","api":"https://new.example.com/provide/vod"},
        {"name":"Duplicate new","api":"https://new.example.com/provide/vod/"},
        {"name":"Invalid","api":"file:///tmp/site.json"},
        {"name":"","api":"https://empty.example.com"}
    ])json";

    const QString message = model.importJsonVideoSites(json, false);

    QVERIFY2(message.startsWith(QStringLiteral("JSON 导入完成：")), qPrintable(message));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.nameAt(0), QString("Updated existing"));
    QCOMPARE(model.typeAt(0), QString("video"));
    QCOMPARE(model.premiumAt(0), true);
    QCOMPARE(model.nameAt(1), QString("New video"));
    QCOMPARE(model.typeAt(1), QString("video"));
    QVERIFY(message.contains(QStringLiteral("去重 1")));
    QVERIFY(message.contains(QStringLiteral("文件内重复 1")));
    QVERIFY(message.contains(QStringLiteral("无效 2")));
    QVERIFY(message.contains(QStringLiteral("检测 0")));
}

void ApiSiteModelTests::rejectsInvalidJsonVideoSites() {
    ApiSiteModel model;
    const int originalCount = model.count();

    const QString malformedMessage = model.importJsonVideoSites(QByteArrayLiteral("not json"), false);
    QVERIFY(malformedMessage.startsWith(QStringLiteral("JSON 导入失败：")));
    QCOMPARE(model.count(), originalCount);

    const QString wrongRootMessage = model.importJsonVideoSites(QByteArrayLiteral("{}"), false);
    QCOMPARE(wrongRootMessage, QStringLiteral("JSON 导入失败：根节点必须是数组"));
    QCOMPARE(model.count(), originalCount);
}

void ApiSiteModelTests::loadsJsonVideoSitesWhenEnabled() {
    if (qEnvironmentVariableIntValue("MELOBOX_RUN_NETWORK_TESTS") != 1)
        QSKIP("Set MELOBOX_RUN_NETWORK_TESTS=1 to run the JSON site test");

    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);
    QSignalSpy finishedSpy(&model, &ApiSiteModel::jsonSitesLoadFinished);

    model.loadJsonVideoSites();

    QCOMPARE(model.jsonSitesLoading(), true);
    QVERIFY2(finishedSpy.wait(20000), "JSON site loading did not finish within 20 seconds");
    QCOMPARE(model.jsonSitesLoading(), false);
    QCOMPARE(finishedSpy.size(), 1);
    const QString message = finishedSpy.takeFirst().at(0).toString();
    QVERIFY2(message.startsWith(QStringLiteral("JSON 导入完成：")), qPrintable(message));
    QVERIFY(model.count() > 0);

    bool hasCheckedSite = false;
    for (int i = 0; i < model.count(); ++i) {
        QCOMPARE(model.typeAt(i), QString("video"));
        if (model.accessStatusAt(i) != 0)
            hasCheckedSite = true;
    }
    QVERIFY(hasCheckedSite);
}

void VideoSearchModelTests::exposesEmptyInitialState() {
    VideoSearchModel model;

    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.currentPage(), 1);
    QCOMPARE(model.totalPages(), 1);
    QCOMPARE(model.errorMessage(), QString());
}

void VideoSearchModelTests::parsesParentCategoryIds() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            socket->readAll();
            const QByteArray body = R"({"total":0,"pagecount":1,"class":[{"type_id":4,"type_pid":0,"type_name":"动漫片"},{"type_id":29,"type_pid":4,"type_name":"国产动漫"}],"list":[]})";
            const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            socket->write(response);
            socket->disconnectFromHost();
        });
    });

    VideoSearchModel model;
    QSignalSpy completedSpy(&model, &VideoSearchModel::searchCompleted);
    model.loadList(QStringLiteral("http://127.0.0.1:%1/api.php/provide/vod/").arg(server.serverPort()),
                   1, {}, true);
    QTRY_COMPARE(completedSpy.count(), 1);

    const QVariantList categories = model.categories();
    QCOMPARE(categories.size(), 2);
    QCOMPARE(categories.at(0).toMap().value(QStringLiteral("typeId")).toString(), QStringLiteral("4"));
    QCOMPARE(categories.at(0).toMap().value(QStringLiteral("parentTypeId")).toString(), QStringLiteral("0"));
    QCOMPARE(categories.at(1).toMap().value(QStringLiteral("typeId")).toString(), QStringLiteral("29"));
    QCOMPARE(categories.at(1).toMap().value(QStringLiteral("parentTypeId")).toString(), QStringLiteral("4"));
}

int main(int argc, char **argv) {
    QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_WIN
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("windows"));
#else
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
#endif
    QGuiApplication app(argc, argv);
    int status = 0;

    if (qEnvironmentVariableIntValue("MELOBOX_API_SITE_TESTS_ONLY") == 1) {
        ApiSiteModelTests tests;
        return QTest::qExec(&tests, argc, argv);
    }

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
