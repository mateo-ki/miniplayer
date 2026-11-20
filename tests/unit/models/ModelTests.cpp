#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QImage>
#include <QTemporaryDir>

#include "app/AppServices.h"
#include "controller/PlayerController.h"
#include "controller/PoolChatController.h"
#include "dmghgv3/DmghgClientV3.h"
#include "media/DmghgClient.h"
#include "infrastructure/Logger.h"
#include "models/ApiSiteModel.h"
#include "models/MediaInfoModel.h"
#include "models/RuntimeLogModel.h"
#include "models/VideoSearchModel.h"
#include "common/MediaUtils.h"
#include "infrastructure/PoolApi.h"

class RuntimeLogModelTests : public QObject {
    Q_OBJECT

private slots:
    void appendsVisibleLogEntries();
    void clearRemovesAllEntries();
};

class MediaUtilsTests : public QObject {
    Q_OBJECT

private slots:
    void sanitizesBrandAndProfanity();
};

class PoolApiTests : public QObject {
    Q_OBJECT

private slots:
    void parsesPoolEnvelope();
    void retriesOnlyWhenNoHttpResponseArrived();
    void retriesOnceWhenTheServerHangsUp();
    void asksForNoHttp2BecauseStreamsGetReset();
};

// 「我的」页存的键是上游编号：新版 18 位、旧版短整数，两种都得能落到详情链路上。
class DmghgHandleKeyTests : public QObject {
    Q_OBJECT

private slots:
    void snowflakeKeyBecomesReusableHandle();
    void snowflakeKeyMapsBackToItself();
    void shortKeyStaysAnOldId();
    void legacyProtocolRefusesSnowflakeKey();
};

// 视图文件里重复的 id 是装载期才报的错，qmlcachegen 编译门会放过 ——
// 装机后的现象就是整个软件打不开（真实踩过：同一块 UI 抄进群聊/私信两份，id 撞了）。
class QmlHygiene : public QObject {
    Q_OBJECT

private slots:
    void noDuplicateIdsInViewFiles();
};

class PoolChatTests : public QObject {
    Q_OBJECT

private slots:
    void blocksSendBeforeHittingTheServer();
    void collectsMentionsLongestNameFirst();
    void reportsLoginRequiredWithoutToken();
    void everyQmlNameResolvesOnTheController();
    void clipboardBitmapBecomesAPngUpload();
    void pastedScreenshotIsScaledBeforeUpload();
    void animeCardChangesWhatCountsAsAnEmptyDraft();
    void appendsOnlyUnseenRowsAndPushesTheCursor();
    void unreadBadgeCountsKeepTheirOwnRules();
    void trimsFromTheOldestEndWhenOverCap();
    void multipartPartMatchesWhatTheServerParses();
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
    void skipActionRules();
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
    void importsSitesFromTextFile();
    void importsBundledSitesOnFirstLaunch();
    void importsJsonVideoSitesWithDeduplication();
    void rejectsInvalidJsonVideoSites();
    void loadsJsonVideoSitesWhenEnabled();
    void removedDefaultSiteStaysDeletedAfterReload();
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

void MediaUtilsTests::sanitizesBrandAndProfanity() {
    QCOMPARE(MediaUtils::sanitizeUserText(QString::fromUtf8("下载请去 www.oneghg.com 官网")),
             QString::fromUtf8("下载请去 *** 官网"));
    QCOMPARE(MediaUtils::sanitizeUserText(QString::fromUtf8("动漫共和国官方")),
             QString::fromUtf8("***官方"));
    QCOMPARE(MediaUtils::sanitizeUserText(QString::fromUtf8("oneghg.com")),
             QStringLiteral("***"));
    QCOMPARE(MediaUtils::sanitizeUserText(QStringLiteral("你个傻逼")),
             QStringLiteral("你个***"));
    QCOMPARE(MediaUtils::sanitizeUserText(QStringLiteral("正常评论")),
             QStringLiteral("正常评论"));
}

void PoolApiTests::parsesPoolEnvelope() {
    const QByteArray okBody = R"({"ok":true,"message":"ok","data":{"items":[{"name":"仙逆"}]}})";
    const PoolApi::Result ok = PoolApi::parseEnvelope(okBody, 200, {});
    QVERIFY2(ok.ok, qPrintable(ok.message));
    QCOMPARE(ok.data.toObject().value(QStringLiteral("items")).toArray().size(), 1);

    const QByteArray failBody = R"({"ok":false,"message":"请先登录","data":null})";
    const PoolApi::Result fail = PoolApi::parseEnvelope(failBody, 401, {});
    QCOMPARE(fail.ok, false);
    QCOMPARE(fail.message, QStringLiteral("请先登录"));

    // 非 JSON (网关错误页/空响应) 要报出状态码，不能当成成功。
    const PoolApi::Result html = PoolApi::parseEnvelope("<html>502</html>", 502, {});
    QCOMPARE(html.ok, false);
    QVERIFY2(html.message.contains(QStringLiteral("502")), qPrintable(html.message));

    // 传输层失败优先返回网络错误文本。
    const PoolApi::Result transport = PoolApi::parseEnvelope({}, 0, QStringLiteral("Connection refused"));
    QCOMPARE(transport.ok, false);
    QCOMPARE(transport.message, QStringLiteral("Connection refused"));

    // ok=false 且没带 message 时不能留下空提示。
    const PoolApi::Result silent = PoolApi::parseEnvelope(R"({"ok":false})", 400, {});
    QCOMPARE(silent.ok, false);
    QVERIFY(!silent.message.isEmpty());
}

void PoolApiTests::retriesOnlyWhenNoHttpResponseArrived() {
    using Err = QNetworkReply::NetworkError;
    // 连接根本没建成：补一次。用户看到的 "Connection closed" 就是这一族。
    QVERIFY(PoolApi::isTransientTransport(Err::RemoteHostClosedError, 0));
    QVERIFY(PoolApi::isTransientTransport(Err::ConnectionRefusedError, 0));
    QVERIFY(PoolApi::isTransientTransport(Err::HostNotFoundError, 0));
    QVERIFY(PoolApi::isTransientTransport(Err::UnknownNetworkError, 0));
    // 服务端答过了就不补：补发会把同一条消息/同一次上报落两遍。
    QVERIFY(!PoolApi::isTransientTransport(Err::InternalServerError, 500));
    QVERIFY(!PoolApi::isTransientTransport(Err::ProtocolUnknownError, 405));
    QVERIFY(!PoolApi::isTransientTransport(Err::UnknownNetworkError, 403));
    // 超时也不补：请求可能已经在服务端落了库，只是回包没赶上。
    QVERIFY(!PoolApi::isTransientTransport(Err::TimeoutError, 0));
    QVERIFY(!PoolApi::isTransientTransport(Err::OperationCanceledError, 0));
    QVERIFY(!PoolApi::isTransientTransport(Err::NoError, 200));
}

void PoolApiTests::retriesOnceWhenTheServerHangsUp() {
    // 量的是真出站链路：第一趟连上一个不给响应就断开的服务器（用户看到的 "Connection closed"），
    // 第二趟才回信封。只测谓词不算测重试。
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int accepted = 0;
    connect(&server, &QTcpServer::newConnection, &server, [&server, &accepted]() {
        auto *socket = server.nextPendingConnection();
        ++accepted;
        const int attempt = accepted;
        connect(socket, &QTcpSocket::readyRead, socket, [socket, attempt]() {
            socket->readAll();
            if (attempt == 1) {
                socket->abort();   // 一个 HTTP 字节都没回
                return;
            }
            const QByteArray body = R"({"ok":true,"message":"ok","data":{"items":[]}})";
            const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            socket->write(response);
            socket->disconnectFromHost();
        });
    });

    QNetworkAccessManager nam;
    bool finished = false;
    bool ok = false;
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    PoolApi::sendRequest(&nam, nam, base, QStringLiteral("/api/anime/favorites"), {},
                         QByteArrayLiteral("GET"), QStringLiteral("token-for-test"),
                         [&finished, &ok](const PoolApi::Result &result) {
                             ok = result.ok;
                             finished = true;
                         });

    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QCOMPARE(accepted, 2);
    QVERIFY2(ok, "补发那一趟应该拿到 ok 信封");
    server.close();
}

// 出站走的是 HTTP/1.1：他日志里那两条 "HTTP/2 protocol error"（22:36 群聊增量、22:43 私信增量）
// 是流被中途 RST —— 没状态码也没响应体，补发那条通道救不了它。
// 这条判据量的是"到 pool 的请求真的没协商成 h2"，量不到网络就直接 SKIP，不装绿。
void PoolApiTests::asksForNoHttp2BecauseStreamsGetReset() {
    const QNetworkRequest request = PoolApi::buildRequest(
        QStringLiteral("https://pool.983698.xyz"), QStringLiteral("/api/chat/unread"), QString());
    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(request);
    QSignalSpy settled(reply, &QNetworkReply::finished);
    if (!settled.wait(15000)) {
        reply->deleteLater();
        QSKIP("这一刻到 pool 的 https 请求没回来，协议协商量不到");
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool usedHttp2 = reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
    reply->deleteLater();
    QVERIFY2(status > 0, qPrintable(QStringLiteral("没拿到状态码，链路不通 (%1)").arg(status)));
    QVERIFY2(!usedHttp2, qPrintable(QStringLiteral("这条请求协商成了 HTTP/2 (status=%1)")
                                        .arg(status)));
}

void DmghgHandleKeyTests::snowflakeKeyBecomesReusableHandle() {
    DmghgClientV3 client;
    // 生产里 vodIdV3 就是这种 18 位号（实测 pool /api/anime/favorites 的 fav_key）。
    const QString snowflake = QStringLiteral("318186713682145386");
    const int handle = client.handleForKey(snowflake);
    QVERIFY2(handle >= 1000000000, qPrintable(QStringLiteral("handle=%1").arg(handle)));
    // 同一个键再取一次必须落回同一句柄，否则点两次同一条会建出两份详情。
    QCOMPARE(client.handleForKey(snowflake), handle);
}

void DmghgHandleKeyTests::snowflakeKeyMapsBackToItself() {
    DmghgClientV3 client;
    const int handle = client.handleForKey(QStringLiteral("347183677302423552"));
    QVERIFY(handle > 0);
    QCOMPARE(client.newIdForHandle(handle), QStringLiteral("347183677302423552"));
}

void DmghgHandleKeyTests::shortKeyStaysAnOldId() {
    DmghgClientV3 client;
    // 短数字是旧整数 id，原样交给 video/idMapping，不能被 intern 成新号。
    QCOMPARE(client.handleForKey(QStringLiteral("2048")), 2048);
    QCOMPARE(client.newIdForHandle(2048), QString());
    QCOMPARE(client.handleForKey(QStringLiteral("   ")), 0);
}

void DmghgHandleKeyTests::legacyProtocolRefusesSnowflakeKey() {
    DmghgClient client;
    QCOMPARE(client.handleForKey(QStringLiteral("318186713682145386")), 0);
    QCOMPARE(client.handleForKey(QStringLiteral("38857")), 38857);
    QCOMPARE(client.handleForKey(QString()), 0);
}

void QmlHygiene::noDuplicateIdsInViewFiles() {
    const QStringList files = {
        QStringLiteral(QT_TESTCASE_SOURCEDIR "/ui/qml/components/MyView.qml"),
        QStringLiteral(QT_TESTCASE_SOURCEDIR "/ui/qml/Main.qml"),
    };
    const QRegularExpression idRe(QStringLiteral("^\\s*id:\\s*([A-Za-z][A-Za-z0-9_]*)\\s*$"));

    for (const QString &path : files) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        QHash<QString, int> seen;
        QStringList clashes;
        for (const QString &line : lines) {
            const auto match = idRe.match(line);
            if (!match.hasMatch())
                continue;
            const QString id = match.captured(1);
            if (seen.value(id) >= 1)
                clashes << QStringLiteral("%1(第%2次)").arg(id).arg(seen.value(id) + 1);
            seen.insert(id, seen.value(id) + 1);
        }
        // 先确认这条判据真的在数东西：一个 id 都没抓到就是正则在空转。
        QVERIFY2(!seen.isEmpty(), qPrintable(QStringLiteral("%1 没解析出任何 id").arg(path)));
        QVERIFY2(seen.size() > 10, qPrintable(QStringLiteral("%1 只解析出 %2 个 id")
                                                  .arg(path).arg(seen.size())));
        QVERIFY2(clashes.isEmpty(),
                 qPrintable(QStringLiteral("%1 里有重复 id，装载期会直接失败: %2")
                                .arg(QFileInfo(path).fileName(), clashes.join(QStringLiteral(", ")))));
    }
}

void PoolChatTests::blocksSendBeforeHittingTheServer() {
    using Chat = PoolChatController;
    const qint64 neverSent = Chat::kMinSendIntervalMs * 100;

    // 空正文（含纯空白）先挡住，别把 400 甩给用户看。
    QVERIFY(!Chat::sendBlockReason(QString(), neverSent).isEmpty());
    QVERIFY(!Chat::sendBlockReason(QStringLiteral("   \n "), neverSent).isEmpty());
    QVERIFY(Chat::sendBlockReason(QString::fromUtf8("\xE4\xBD\xA0\xE5\xA5\xBD"), neverSent).isEmpty());

    // 正文长度按 unicode 标量计，跟服务端 chars().count() 对齐：
    // 一个 emoji（UTF-16 里占 2 个码元）只算 1 字，所以正好 500 个能发、501 个才拦。
    const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
    QVERIFY(Chat::sendBlockReason(emoji.repeated(Chat::kMaxContentLength), neverSent).isEmpty());
    const QString tooLong = Chat::sendBlockReason(emoji.repeated(Chat::kMaxContentLength + 1), neverSent);
    QVERIFY2(tooLong.contains(QString::number(Chat::kMaxContentLength)), qPrintable(tooLong));

    // 汉字 501 个同样按超限处理，措辞与服务端一致。
    QVERIFY(!Chat::sendBlockReason(QString(Chat::kMaxContentLength + 1,
                                           QChar(0x4ED9)), neverSent).isEmpty());

    // 2 秒间隔：没到点就给服务端那句原文。
    const QString tooFast = Chat::sendBlockReason(QString::fromUtf8("\xE5\x86\x8D\xE5\x8F\x91\xE4\xB8\x80\xE6\x9D\xA1"),
                                                  Chat::kMinSendIntervalMs - 1);
    QVERIFY2(tooFast.contains(QString::fromUtf8("\xE5\x8F\x91\xE9\x80\x81\xE5\xA4\xAA\xE5\xBF\xAB")),
             qPrintable(tooFast));
    QVERIFY(Chat::sendBlockReason(QString::fromUtf8("\xE5\x86\x8D\xE5\x8F\x91\xE4\xB8\x80\xE6\x9D\xA1"),
                                  Chat::kMinSendIntervalMs).isEmpty());
}

void PoolChatTests::collectsMentionsLongestNameFirst() {
    const QString liming = QString::fromUtf8("\xE6\x9D\x8E\xE6\x98\x8E"); // 李明
    const QString li = QString::fromUtf8("\xE6\x9D\x8E");                 // 李
    const QString wang = QString::fromUtf8("\xE7\x8E\x8B");               // 王

    const QVariantList users = {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("u1")}, {QStringLiteral("username"), liming}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("u2")}, {QStringLiteral("username"), li}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("u3")}, {QStringLiteral("username"), wang}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("u4")}, {QStringLiteral("username"), QString()}},
    };

    // 命中 "@李明" 之后要把这段划掉，短名 "@李" 不能再吃第二次；空名候选人直接跳过。
    const QJsonArray hits = PoolChatController::mentionedUserIds(
        QStringLiteral("@") + liming + QStringLiteral(" ok @") + wang, users);
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("u1"));
    QCOMPARE(hits.at(1).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("u3"));

    // 同一个人被 @ 两次只上报一次；没 @ 到就是空数组，正文不带 mentions。
    QCOMPARE(PoolChatController::mentionedUserIds(QStringLiteral("@") + li + QStringLiteral(" @") + li,
                                                  users).size(), 1);
    QCOMPARE(PoolChatController::mentionedUserIds(QStringLiteral("ping"), users).size(), 0);
}

void PoolChatTests::reportsLoginRequiredWithoutToken() {
    // 没有 AuthController（也就没有会话 Cookie）时一个请求都不发，直接给登录提示。
    PoolChatController controller(nullptr);
    QSignalSpy spy(&controller, &PoolChatController::groupChanged);
    controller.refreshGroup();
    QCOMPARE(controller.groupMessage(), QString::fromUtf8("\xE8\xAF\xB7\xE5\x85\x88\xE7\x99\xBB\xE5\xBD\x95"));
    QCOMPARE(controller.messages().size(), 0);
    QVERIFY(spy.count() >= 1);
    QCOMPARE(controller.maxContentLength(), PoolChatController::kMaxContentLength);

    // 关掉页面时轮询必须停：setActive(false) 之后定时器不再排下一次。
    controller.setActive(true);
    controller.setActive(false);
    QVERIFY(!controller.active());
}

void PoolChatTests::animeCardChangesWhatCountsAsAnEmptyDraft() {
    PoolChatController controller(nullptr);

    // 空草稿：客户端那句要和服务端 chat_send 一模一样，不能各说一套。
    QCOMPARE(controller.groupBlockReason(QString()), QStringLiteral("消息内容、图片和动漫至少填一项"));

    // 挂了 ## 选中的卡片 = 有内容，空草稿放行；卡片名走服务端同一套 60 字截断。
    controller.pickAnime(QStringLiteral("仙逆"),
                         QStringLiteral("https://img.example/a.jpg"),
                         QStringLiteral("318186697634738205"));
    QCOMPARE(controller.pendingAnimeName(), QStringLiteral("仙逆"));
    QCOMPARE(controller.pendingAnimeId(), QStringLiteral("318186697634738205"));
    QCOMPARE(controller.groupBlockReason(QString()), QString());

    // 长度和 2 秒限速不该因为卡片松口。
    QCOMPARE(controller.groupBlockReason(QString(501, QLatin1Char('a'))).isEmpty(), false);
    controller.clearAnime();
    QVERIFY(controller.pendingAnimeId().isEmpty());

    // 没有片名的选择不算挂上卡片（服务端 name 必填）。
    controller.pickAnime(QString(), QStringLiteral("x"), QStringLiteral("1"));
    QVERIFY(controller.pendingAnimeId().isEmpty());
}

void PoolChatTests::appendsOnlyUnseenRowsAndPushesTheCursor() {
    QVariantList list;
    QSet<QString> seen;
    QString cursor;

    auto stamp = [](const char *id, const char *at) {
        QVariantMap message;
        message.insert(QStringLiteral("id"), QLatin1String(id));
        message.insert(QStringLiteral("created_at"), QLatin1String(at));
        return message;
    };

    // 全量那一趟：按顺序接进来，游标落到这一批里最新的一条。
    const QList<QVariantMap> first{stamp("m1", "2026-10-19T13:00:00+00:00"),
                                   stamp("m2", "2026-10-19T13:00:05+00:00")};
    QCOMPARE(PoolChatController::appendUnseenStamps(list, seen, cursor, first, 400), true);
    QCOMPARE(list.size(), 2);
    QCOMPARE(cursor, QStringLiteral("2026-10-19T13:00:05+00:00"));

    // after 游标是 >= 比较：边界那条必然再回来一次，不能变成两条一模一样的气泡。
    const QList<QVariantMap> second{stamp("m2", "2026-10-19T13:00:05+00:00"),
                                    stamp("m3", "2026-10-19T13:00:09+00:00")};
    QCOMPARE(PoolChatController::appendUnseenStamps(list, seen, cursor, second, 400), true);
    QCOMPARE(list.size(), 3);

    // 整批都是见过的：返回 false，调用方就不发信号，delegate 不会被轮询重建。
    const QList<QVariantMap> third{stamp("m3", "2026-10-19T13:00:09+00:00")};
    QCOMPARE(PoolChatController::appendUnseenStamps(list, seen, cursor, third, 400), false);
    QCOMPARE(list.size(), 3);

    // 游标只准往前挪：回包里先来新的后来旧的，也不能把它带回去，
    // 否则下一轮会把已经看过的一整段再拉一遍。
    const QList<QVariantMap> fourth{stamp("m5", "2026-10-19T14:00:00+00:00"),
                                    stamp("m4", "2026-10-19T12:59:00+00:00")};
    QCOMPARE(PoolChatController::appendUnseenStamps(list, seen, cursor, fourth, 400), true);
    QCOMPARE(cursor, QStringLiteral("2026-10-19T14:00:00+00:00"));
}

void PoolChatTests::unreadBadgeCountsKeepTheirOwnRules() {
    const QJsonObject full{
        {QStringLiteral("group"), 4},
        {QStringLiteral("dm"), 2},
    };
    QCOMPARE(PoolChatController::unreadPairOf(full, false).first, 4);
    QCOMPARE(PoolChatController::unreadPairOf(full, false).second, 2);

    // 正在看群聊那一屏：群聊那颗不该亮，私信那颗照旧。
    QCOMPARE(PoolChatController::unreadPairOf(full, true).first, 0);
    QCOMPARE(PoolChatController::unreadPairOf(full, true).second, 2);

    // 缺键 = 0（不是"留着上次的数"），不是数字 = 0，负数也当 0。
    QCOMPARE(PoolChatController::unreadPairOf(QJsonObject(), false).first, 0);
    QJsonObject junk;
    junk.insert(QStringLiteral("group"), QStringLiteral("4"));
    junk.insert(QStringLiteral("dm"), -3);
    QCOMPARE(PoolChatController::unreadPairOf(junk, false).first, 0);
    QCOMPARE(PoolChatController::unreadPairOf(junk, false).second, 0);
}

void PoolChatTests::trimsFromTheOldestEndWhenOverCap() {
    QVariantList list;
    QSet<QString> seen;
    QString cursor;

    auto stamp = [](const QString &id, const QString &at) {
        QVariantMap message;
        message.insert(QStringLiteral("id"), id);
        message.insert(QStringLiteral("created_at"), at);
        return message;
    };

    QList<QVariantMap> batch;
    for (int i = 1; i <= 5; ++i)
        batch.append(stamp(QString::number(i),
                           QStringLiteral("2026-10-19T13:00:0%1+00:00").arg(i)));
    QCOMPARE(PoolChatController::appendUnseenStamps(list, seen, cursor, batch, 3), true);
    QCOMPARE(list.size(), 3);
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("3"));
    QCOMPARE(list.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("5"));
    // 裁掉的必须同时从 seen 里去掉，不然 seen 只增不减会一路涨。
    QCOMPARE(seen.contains(QStringLiteral("1")), false);
    QCOMPARE(seen.contains(QStringLiteral("5")), true);
    QCOMPARE(cursor, QStringLiteral("2026-10-19T13:00:05+00:00"));
}

void PoolChatTests::multipartPartMatchesWhatTheServerParses() {
    // PNG 魔数里就有 \r\n，用十六进制拼，别在源码里写转义。
    const QByteArray bytes = QByteArray::fromHex("89504e470d0a1a0a") + QByteArray("fake-payload");
    const PoolChatController::MultipartBody part =
        PoolChatController::buildImagePart(QStringLiteral("C:/Users/me/我 的图.png"), bytes);

    const QByteArray prefix = QByteArrayLiteral("multipart/form-data; boundary=");
    QVERIFY2(part.contentType.startsWith(prefix), part.contentType.constData());
    const QByteArray boundary = part.contentType.mid(prefix.size());
    QVERIFY(boundary.size() > 8);

    QVERIFY2(part.body.startsWith("--" + boundary + "\r\n"), part.body.left(80).constData());
    QVERIFY2(part.body.endsWith("\r\n--" + boundary + "--\r\n"), part.body.right(80).constData());
    // 字段名要和 upload.rs 取的第一段对得上，文件名只留 basename。
    QVERIFY(part.body.contains(
        "Content-Disposition: form-data; name=\"file\"; filename=\"我 的图.png\""));
    QVERIFY(part.body.contains("Content-Type: application/octet-stream\r\n"));
    // 原字节一个不少地进去（图片不是文本，任何转码都会坏）。
    QVERIFY(part.body.contains(bytes));

    // 文件名里的引号会把 form-data 那一行拆坏，必须剥掉。
    const PoolChatController::MultipartBody quoted =
        PoolChatController::buildImagePart(QStringLiteral("a\"b.png"), bytes);
    QVERIFY(!quoted.body.contains("a\"b.png"));
    QVERIFY(quoted.body.contains("filename=\"ab.png\""));
}

void PoolChatTests::clipboardBitmapBecomesAPngUpload() {
    // 剪贴板来的位图必须转成服务端认得的 PNG：upload.rs 只按魔数认类型。
    QImage image(8, 6, QImage::Format_ARGB32);
    image.fill(QColor(10, 20, 30));
    const QByteArray png = PoolChatController::pngBytes(image);
    QVERIFY2(!png.isEmpty(), "转换结果为空");
    QCOMPARE(png.left(8), QByteArray::fromHex("89504e470d0a1a0a"));

    // 空图（剪贴板里没图时 clipboard->image() 就是这个）不能产出字节。
    QVERIFY(PoolChatController::pngBytes(QImage()).isEmpty());
}

void PoolChatTests::pastedScreenshotIsScaledBeforeUpload() {
    // 2560x1440 的截图直接编 PNG 是几 MB，卡的就是这一步：长边必须先压到 1600 以内。
    QImage big(2560, 1440, QImage::Format_ARGB32);
    big.fill(QColor(120, 160, 200));
    const QByteArray png = PoolChatController::pngBytes(big);
    QVERIFY2(!png.isEmpty(), "转换结果为空");

    QImage back;
    QVERIFY(QImage::fromData(png, "PNG").isNull() == false);
    back = QImage::fromData(png, "PNG");
    QVERIFY2(qMax(back.width(), back.height()) <= 1600,
             qPrintable(QStringLiteral("压完还有 %1x%2").arg(back.width()).arg(back.height())));
    QVERIFY2(qAbs(back.width() * 1440 - back.height() * 2560) <= 1440,
             qPrintable(QStringLiteral("压完比例变了 %1x%2").arg(back.width()).arg(back.height())));
    QVERIFY2(png.size() < 512 * 1024, qPrintable(QStringLiteral("PNG %1 字节").arg(png.size())));

    // 小图不该被放大：原样出去。
    QImage small(64, 48, QImage::Format_ARGB32);
    small.fill(QColor(1, 2, 3));
    QImage smallBack = QImage::fromData(PoolChatController::pngBytes(small), "PNG");
    QCOMPARE(smallBack.width(), 64);
    QCOMPARE(smallBack.height(), 48);
}

void PoolChatTests::everyQmlNameResolvesOnTheController() {
    // 视图里写的每一个 poolChat.xxx 都得真挂在控制器的元对象上。
    // 属性少一个时 QML 不报编译错，只把 undefined 赋给 string 属性，那行字永远空着 ——
    // 装机版实测就踩过 threadMessage 这一条，所以这条判据直接读源文件，不靠我列名单。
    // 侧栏那颗红点是在 Main.qml 里读 services.poolChat.unreadTotal 的：名字写错只会静默不亮，
    // 所以扫描范围要连主文件一起吃进来，不能只盯 MyView.qml。
    const QStringList qmlFiles = {
        QStringLiteral(QT_TESTCASE_SOURCEDIR "/ui/qml/components/MyView.qml"),
        QStringLiteral(QT_TESTCASE_SOURCEDIR "/ui/qml/Main.qml"),
    };
    QString source;
    for (const QString &path : qmlFiles) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
        source += QString::fromUtf8(file.readAll());
    }

    QRegularExpression re(QStringLiteral("poolChat\\.([A-Za-z][A-Za-z0-9_]*)"));
    QSet<QString> names;
    auto match = re.globalMatch(source);
    while (match.hasNext())
        names.insert(match.next().captured(1));
    // 名单一个都没抓到就是判据在空转，先把数量卡住。
    QVERIFY2(names.size() >= 20, qPrintable(QStringLiteral("caught %1 names").arg(names.size())));

    const QMetaObject *meta = &PoolChatController::staticMetaObject;
    QStringList missing;
    for (const QString &name : std::as_const(names)) {
        bool found = meta->indexOfProperty(qPrintable(name)) >= 0;
        for (int i = 0; !found && i < meta->methodCount(); ++i)
            found = QLatin1String(meta->method(i).name()) == name;
        if (!found)
            missing << name;
    }
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("MyView 用到但 PoolChatController 上没有: %1")
                            .arg(missing.join(QStringLiteral(", ")))));
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
    AppServices services;
    PlayerController controller(&services);

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
    AppServices services;
    PlayerController controller(&services);

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
    AppServices services;
    PlayerController controller(&services);
    const QString url = QStringLiteral("https://media.example.com/episode.m3u8");

    controller.playVodUrl(url);

    QCOMPARE(controller.currentFile(), url);
    QCOMPARE(controller.loading(), true);
}

void PlayerControllerTests::skipActionRules() {
    const auto act = [](qint64 pos, qint64 dur, int intro, int outro, bool introDone, bool outroDone) {
        return static_cast<int>(
            PlayerController::skipAction(pos, dur, intro, outro, introDone, outroDone));
    };
    const int none = static_cast<int>(PlayerController::SkipAction::None);
    const int jumpIntro = static_cast<int>(PlayerController::SkipAction::JumpIntro);
    const int finish = static_cast<int>(PlayerController::SkipAction::FinishEpisode);

    const qint64 episodeMs = 25 * 60 * 1000;
    const int introMs = 85 * 1000;
    const int outroMs = 30 * 1000;

    // 一集从头播: 片头内跳一次，之后不再重复。
    QCOMPARE(act(1000, episodeMs, introMs, outroMs, false, false), jumpIntro);
    QCOMPARE(act(1000, episodeMs, introMs, outroMs, true, false), none);
    // 已经播过片头位置就不回头跳。
    QCOMPARE(act(10 * 60 * 1000, episodeMs, introMs, outroMs, false, false), none);
    // 进入片尾窗口按"播完"处理，一次为限。
    QCOMPARE(act(episodeMs - 20 * 1000, episodeMs, introMs, outroMs, true, false), finish);
    QCOMPARE(act(episodeMs - 20 * 1000, episodeMs, introMs, outroMs, true, true), none);
    // 两个开关都为 0 时完全不介入。
    QCOMPARE(act(1000, episodeMs, 0, 0, false, false), none);
    QCOMPARE(act(episodeMs - 1000, episodeMs, 0, 0, false, false), none);
    // 时长未知 (直播/还没出媒体信息) 不动。
    QCOMPARE(act(1000, 0, introMs, outroMs, false, false), none);
    // 短片不被片头推进到后半段。
    QCOMPARE(act(1000, 100 * 1000, introMs, outroMs, false, false), none);
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

void ApiSiteModelTests::importsSitesFromTextFile() {
    // 随包分发的 vod.txt 就是这套加密分享串，用固定样本文件离线验证导入路径。
    const QString samplePath = QFINDTESTDATA(QStringLiteral("vod_sample.txt"));
    QVERIFY2(!samplePath.isEmpty(), "vod_sample.txt fixture not found next to the test source");

    ApiSiteModel model;
    while (model.count() > 0)
        model.removeAt(0);

    const QString importMessage = model.importSitesFromFile(samplePath);
    QVERIFY2(importMessage.startsWith(QStringLiteral("Import complete:")), qPrintable(importMessage));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.nameAt(0), QString("Sample Video"));
    QCOMPARE(model.premiumAt(0), true);
    QCOMPARE(model.typeAt(1), QString("image"));

    // 空文件/缺文件都只报错，不动已有站点。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString emptyPath = dir.path() + QStringLiteral("/vod.txt");
    {
        QFile file(emptyPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    }
    QVERIFY(model.importSitesFromFile(emptyPath).startsWith(QStringLiteral("加载站点失败")));
    QVERIFY(model.importSitesFromFile(dir.path() + QStringLiteral("/missing-vod.txt"))
                .startsWith(QStringLiteral("加载站点失败")));
    QCOMPARE(model.count(), 2);
}

void ApiSiteModelTests::importsBundledSitesOnFirstLaunch() {
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/api_sites.json");
    QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/vod.txt"),
        QDir::currentPath() + QStringLiteral("/vod.txt"),
    };
    QString bundledPath;
    for (const QString &candidate : std::as_const(candidates)) {
        if (QFileInfo::exists(candidate)) {
            bundledPath = candidate;
            break;
        }
    }
    if (bundledPath.isEmpty())
        QSKIP("vod.txt not deployed next to the test binary");

    QFile::remove(configPath);
    ApiSiteModel model;

    QVERIFY2(model.count() > 1, qPrintable(QStringLiteral("bundled import gave %1").arg(model.count())));
    // 命中本地清单就不该再走网络：远端拉取只是文件缺失/解不开时的兜底。
    QCOMPARE(model.remoteSitesLoading(), false);
    QVERIFY(!model.currentVideoBaseUrl().isEmpty());

    // 导入结果必须落盘，否则下次启动还会再引导一遍。
    ApiSiteModel reloaded;
    QCOMPARE(reloaded.count(), model.count());
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

void ApiSiteModelTests::removedDefaultSiteStaysDeletedAfterReload() {
    // 用户删除默认站点后必须写墓碑：重启 (重新构造 ApiSiteModel 触发
    // ensureDefaults) 时不得把删掉的默认站点补回。
    const QString heiliaoUrl = QStringLiteral("http://api.yujn.cn/api/cxk.php?");
    {
        ApiSiteModel model;
        int index = -1;
        for (int i = 0; i < model.count(); ++i) {
            if (model.baseUrlAt(i) == heiliaoUrl) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            // 前序测试已把该默认站点连同墓碑一起清掉时补一个再删。
            model.add("Heiliao image", heiliaoUrl, QStringLiteral("image"));
            index = model.count() - 1;
        }
        model.removeAt(index);
        for (int i = 0; i < model.count(); ++i)
            QVERIFY2(model.baseUrlAt(i) != heiliaoUrl, "site not removed in-session");
    }

    ApiSiteModel reloaded;
    for (int i = 0; i < reloaded.count(); ++i)
        QVERIFY2(reloaded.baseUrlAt(i) != heiliaoUrl,
                 "deleted default site was resurrected by ensureDefaults");
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

    if (qEnvironmentVariableIntValue("MEDIAUTILS_TESTS_ONLY") == 1) {
        MediaUtilsTests tests;
        return QTest::qExec(&tests, argc, argv);
    }

    if (qEnvironmentVariableIntValue("MELOBOX_API_SITE_TESTS_ONLY") == 1) {
        ApiSiteModelTests tests;
        return QTest::qExec(&tests, argc, argv);
    }

    if (qEnvironmentVariableIntValue("MELOBOX_PLAYER_TESTS_ONLY") == 1) {
        PlayerControllerTests tests;
        return QTest::qExec(&tests, argc, argv);
    }

    if (qEnvironmentVariableIntValue("MELOBOX_POOL_API_TESTS_ONLY") == 1) {
        PoolApiTests tests;
        return QTest::qExec(&tests, argc, argv);
    }

    if (qEnvironmentVariableIntValue("MELOBOX_POOL_CHAT_TESTS_ONLY") == 1) {
        PoolChatTests tests;
        return QTest::qExec(&tests, argc, argv);
    }

    if (qEnvironmentVariableIntValue("MELOBOX_QML_HYGIENE_TESTS_ONLY") == 1) {
        QmlHygiene tests;
        return QTest::qExec(&tests, argc, argv);
    }

    if (qEnvironmentVariableIntValue("MELOBOX_HANDLE_KEY_TESTS_ONLY") == 1) {
        DmghgHandleKeyTests tests;
        return QTest::qExec(&tests, argc, argv);
    }

    {
        RuntimeLogModelTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        MediaUtilsTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        PoolApiTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        PoolChatTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        DmghgHandleKeyTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        QmlHygiene tests;
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
