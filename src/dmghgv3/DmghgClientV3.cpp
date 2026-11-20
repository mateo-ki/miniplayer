#include "DmghgClientV3.h"

#include "common/MediaUtils.h"

#include "infrastructure/Logger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>

#include <memory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace {

// 网关域名不在 APK 里，由 app 的 addressing 层运行时解析（设备侧
// files/addressing/<dni>/envelope 的 polyvision 组）。官方换域名时用
// QSettings dmghg/apiHostV3 覆盖即可，无需重编译。
// 正文本身是 V3 信封，但传输层仍走 https：早先"响应被夹脏字节"的现象与传输无关，
// 是 ChaCha 多块 feed-forward 用错了规则（见 MagicCrypto.cpp 的说明），已修好。
const QString kDefaultApiHost = QStringLiteral("https://rgxbm.xiaoxiangkj.com");
const QString kAppId = QStringLiteral("315891530526580736");
const QString kUserAgent = QStringLiteral("Myuko Ktor Client");
const QString kPlatform = QStringLiteral("android");
constexpr int kTimeoutMs = 8000;
constexpr int kBootstrapGenProbe = 1;

QString apiHost() {
    QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    const QString overrideHost = s.value(QStringLiteral("dmghg/apiHostV3")).toString().trimmed();
    return overrideHost.isEmpty() ? kDefaultApiHost : overrideHost;
}

QString strF(const QJsonObject &o, const char *key) {
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toInt());
    return {};
}

qint64 hexColorToArgb(const QString &hex) {
    bool ok = false;
    const quint32 rgb = hex.trimmed().remove(QLatin1Char('#')).toUInt(&ok, 16);
    return ok ? qint64(0xFF000000ull | rgb) : qint64(0xFFFFFFFFull);
}

QString fontSizeToName(int size) {
    if (size <= 0) return QStringLiteral("M");
    return size < 20 ? QStringLiteral("S") : (size <= 28 ? QStringLiteral("M") : QStringLiteral("L"));
}

// 旧接口 sort 取值 -> 新接口 (latest|hottest|score|...)。
QString mapSort(const QString &sort) {
    const QString s = sort.toLower();
    if (s.isEmpty() || s == QLatin1String("hot") || s == QLatin1String("hits"))
        return QStringLiteral("hottest");
    if (s == QLatin1String("new") || s == QLatin1String("newest") || s == QLatin1String("latest"))
        return QStringLiteral("latest");
    return s;
}

// "第01集"/"EP12" -> 12；解析不出返回 0。
int trailingNumber(const QString &s) {
    int end = s.size();
    while (end > 0 && !s.at(end - 1).isDigit()) --end;
    int start = end;
    while (start > 0 && s.at(start - 1).isDigit()) --start;
    return start < end ? s.mid(start, end - start).toInt() : 0;
}

// 解析更新周期文本（"160|周日18:40更"、"334|二四六10:00更"），返回周几数字列表（1=周一..7=周日）
QVector<int> parseScheduleWeekdays(const QString &schedule) {
    const int pipeIdx = schedule.indexOf(QLatin1Char('|'));
    if (pipeIdx < 0) return {};
    QString rest = schedule.mid(pipeIdx + 1).trimmed();
    if (rest.startsWith(QStringLiteral("周")))
        rest = rest.mid(1);
    QVector<int> days;
    for (const QChar &ch : rest) {
        int day = 0;
        if (ch == QChar(u'一')) day = 1;
        else if (ch == QChar(u'二')) day = 2;
        else if (ch == QChar(u'三')) day = 3;
        else if (ch == QChar(u'四')) day = 4;
        else if (ch == QChar(u'五')) day = 5;
        else if (ch == QChar(u'六')) day = 6;
        else if (ch == QChar(u'日') || ch == QChar(u'天')) day = 7;
        else break;
        if (!days.contains(day))
            days.append(day);
    }
    return days;
}

QVariantList videoItems(const QJsonArray &arr) {
    QVariantList out;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        QVariantMap m;
        m.insert(QStringLiteral("vodId"), strF(o, "id"));
        m.insert(QStringLiteral("vodName"),
                 MediaUtils::sanitizeUserText(strF(o, "title").isEmpty() ? strF(o, "name") : strF(o, "title")));
        m.insert(QStringLiteral("vodPic"),
                 strF(o, "coverUrl").isEmpty() ? strF(o, "pic") : strF(o, "coverUrl"));
        const int cur = o.value(QLatin1String("currentEpisodes")).toInt();
        const int total = o.value(QLatin1String("totalEpisodes")).toInt();
        const bool finished = o.value(QLatin1String("isFinished")).toInt() != 0;
        QString remarks = strF(o, "continu");
        if (remarks.isEmpty() && cur > 0)
            remarks = finished ? QStringLiteral("%1集全").arg(total > 0 ? total : cur)
                               : QStringLiteral("更新至%1集").arg(cur);
        m.insert(QStringLiteral("vodRemarks"), remarks);
        m.insert(QStringLiteral("schedule"), strF(o, "updateSchedule"));
        m.insert(QStringLiteral("typeName"),
                 strF(o, "categoryName").isEmpty() ? strF(o, "types") : strF(o, "categoryName"));
        m.insert(QStringLiteral("vodYear"), strF(o, "year"));
        m.insert(QStringLiteral("vodArea"), strF(o, "area"));
        out.append(m);
    }
    return out;
}

}  // namespace

DmghgClientV3::DmghgClientV3(QObject *parent) : DmghgClient(parent) {}

QString DmghgClientV3::deviceId() {
    QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    QString id = s.value(QStringLiteral("dmghg/deviceIdV3")).toString().trimmed();
    if (id.size() == 64) return id;
    // 形状与真实设备指纹一致（64 位十六进制），值本身是本客户端自己的身份标签，
    // 触发风控时不牵连真实设备 —— 与旧客户端刻意用非官方 appid 的思路一致。
    QByteArray raw(32, '\0');
    for (int i = 0; i < 32; ++i) raw[i] = char(QRandomGenerator::system()->bounded(256));
    id = QString::fromLatin1(raw.toHex());
    s.setValue(QStringLiteral("dmghg/deviceIdV3"), id);
    Logger::instance().info(QStringLiteral("[DmghgV3] generated deviceId %1…").arg(id.left(12)));
    return id;
}

void DmghgClientV3::ensureSession(const std::function<void(bool, const QString &)> &done) {
    if (cfg_.valid()) {
        if (done) done(true, {});
        return;
    }
    if (done) waiters_.append(done);
    if (bootstrapping_) return;
    bootstrapping_ = true;
    loadBootstrap([this](bool ok, const QString &err) {
        bootstrapping_ = false;
        QVector<std::function<void(bool, const QString &)>> waiters;
        waiters.swap(waiters_);
        for (auto &w : waiters) w(ok, err);
    });
}

void DmghgClientV3::loadBootstrap(std::function<void(bool, const QString &)> done) {
    const QString url = QStringLiteral("%1/api/v1/app/client/auth/bootstrap.jpg?appId=%2&sv=1&_t=%3")
                            .arg(apiHost(), kAppId)
                            .arg(QDateTime::currentSecsSinceEpoch());
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(kTimeoutMs);
    req.setRawHeader("User-Agent", kUserAgent.toUtf8());
    req.setRawHeader("Accept", "image/jpeg");
    req.setRawHeader("Accept-Encoding", "identity");
    req.setRawHeader("X-App-Id", kAppId.toUtf8());
    req.setRawHeader("X-Platform", kPlatform.toUtf8());
    // 必须带 X-Device-Id：不带时服务端返回一张通用诱饵图，其隐写载荷用任何
    // bootstrap 代际密钥都解不开（实测 2577B 诱饵 vs 2737B 真配置）。
    req.setRawHeader("X-Device-Id", deviceId().toUtf8());
    auto *reply = nam_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        const bool netOk = reply->error() == QNetworkReply::NoError;
        const QByteArray jpeg = reply->readAll();
        reply->deleteLater();
        if (!netOk) {
            if (done) done(false, QStringLiteral("bootstrap 请求失败"));
            return;
        }
        const int base = dmghg::bootstrapGeneration(QDateTime::currentSecsSinceEpoch());
        QString lastErr;
        for (int d = 0; d <= kBootstrapGenProbe; ++d) {
            const int candidates[3] = {base, base - d, base + d};
            for (int gen : candidates) {
                auto cfg = dmghg::decodeBootstrap(jpeg, kAppId.toUtf8(), gen, &lastErr);
                if (!cfg) continue;
                cfg_ = *cfg;
                secret_ = dmghg::deriveSecret(cfg_.masterSecret, deviceId());
                Logger::instance().info(QStringLiteral(
                    "[DmghgV3] bootstrap ok (gen=%1 keyVersion=%2 clockSkew=%3s)")
                    .arg(gen).arg(cfg_.keyVersion)
                    .arg(cfg_.serverTimeMs / 1000 - QDateTime::currentSecsSinceEpoch()));
                if (done) done(true, {});
                return;
            }
        }
        if (done) done(false, QStringLiteral("bootstrap 解码失败: %1").arg(lastErr));
    });
}

void DmghgClientV3::post(const QString &path, const QJsonObject &body, const JsonCb &cb) {
    // 进程内偶发单字节损坏(仅 app 环境, 独立 harness 从不复现)会导致响应解析失败;
    // 换新连接重发通常即可成功。
    postAttempt(path, body, cb, 0);
}

void DmghgClientV3::postAttempt(const QString &path, const QJsonObject &body, const JsonCb &cb, int attempt) {
    ensureSession([this, path, body, cb, attempt](bool ok, const QString &err) {
        if (!ok) { cb(false, {}, err); return; }
        const QByteArray plain = QJsonDocument(body).toJson(QJsonDocument::Compact);
        const QByteArray envelope =
            dmghg::sealEnvelope(secret_, plain, QDateTime::currentSecsSinceEpoch());
        if (envelope.isEmpty()) {
            Logger::instance().warn(QStringLiteral("[DmghgV3] %1 seal failed").arg(path));
            cb(false, {}, QStringLiteral("信封封装失败"));
            return;
        }

        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        const QString nonce = QString::fromLatin1(QCryptographicHash::hash(
            QByteArray::number(ts) + path.toUtf8(), QCryptographicHash::Md5).toHex());
        QNetworkRequest req{QUrl(apiHost() + path)};
        req.setTransferTimeout(kTimeoutMs);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        req.setRawHeader("Accept", "application/json");
        req.setRawHeader("Accept-Charset", "UTF-8");
        req.setRawHeader("Accept-Encoding", "identity");
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        // HTTP/1.1 + keep-alive：复用同一条 TLS 连接，实测第 2 个请求起从 ~600ms 降到 ~320ms，
        // 响应体逐字节完好（h2 下没验过，维持 HTTP/1.1 的按长度分帧）。
        req.setRawHeader("User-Agent", kUserAgent.toUtf8());
        req.setRawHeader("X-App-Id", kAppId.toUtf8());
        req.setRawHeader("X-Platform", kPlatform.toUtf8());
        req.setRawHeader("X-Timestamp", QByteArray::number(ts));
        req.setRawHeader("X-Nonce", nonce.toUtf8());
        req.setRawHeader("X-Signature",
                         dmghg::makeSignature(ts, nonce, kAppId, cfg_.signSecret).toUtf8());
        req.setRawHeader("X-Device-Id", deviceId().toUtf8());

        auto *reply = nam_.post(req, envelope);
        auto started = std::make_shared<QElapsedTimer>();
        started->start();
        // 首字节时刻用来区分"连接/排队慢"和"传输慢"（Fake-IP、代理、主线程争用都只影响前者）。
        auto firstByteMs = std::make_shared<qint64>(-1);
        connect(reply, &QNetworkReply::downloadProgress, this,
                [firstByteMs, started](qint64 received, qint64) {
                    if (*firstByteMs < 0 && received > 0) *firstByteMs = started->elapsed();
                });
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, path, cb, body, attempt, started, firstByteMs]() {
            // 先读完再 deleteLater：顺序反了会读到已回收的缓冲。
            if (reply->error() != QNetworkReply::NoError) {
                const QString err = reply->errorString();
                Logger::instance().warn(QStringLiteral("[DmghgV3] %1 net fail: %2 (http=%3 len=%4 attempt=%5)")
                    .arg(path, err)
                    .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                    .arg(reply->bytesAvailable()).arg(attempt));
                reply->deleteLater();
                if (attempt < 2) { postAttempt(path, body, cb, attempt + 1); return; }
                cb(false, {}, QStringLiteral("%1 请求失败: %2").arg(path, err));
                return;
            }
            const qint64 netMs = started->elapsed();
            const QByteArray wire = reply->readAll();
            const QByteArray xenc = reply->rawHeader("X-Encrypted");
            const qint64 ttfb = *firstByteMs;
            reply->deleteLater();

            // 响应按约定是 V3 信封（首字节 0x03），以 X-Encrypted 头为准 —— 与 app 一致。
            QByteArray raw = wire;
            if (!xenc.isEmpty()) {
                QString err;
                auto plain = dmghg::openEnvelope(secret_, raw, &err);
                if (!plain) {
                    // 标签不过最常见原因是服务端已换密钥 (keyVersion 递增)：作废会话重 bootstrap。
                    Logger::instance().warn(QStringLiteral("[DmghgV3] %1 open failed: %2").arg(path, err));
                    cfg_ = {};
                    if (attempt < 1) { postAttempt(path, body, cb, attempt + 1); return; }
                    cb(false, {}, QStringLiteral("响应解密失败: %1").arg(err));
                    return;
                }
                raw = *plain;
            }
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
            if (!doc.isObject()) {
                Logger::instance().warn(QStringLiteral(
                    "[DmghgV3] %1 非 JSON: wire=%2 xenc=%3 plain=%4 head=%5 off=%6 (%7) attempt=%8")
                    .arg(path).arg(wire.size()).arg(QString::fromLatin1(xenc)).arg(raw.size())
                    .arg(QString::fromLatin1(raw.left(16).toHex()))
                    .arg(pe.offset).arg(pe.errorString()).arg(attempt));
                QFile dump(QStringLiteral("dmghg_bad_body_%1.bin").arg(attempt));
                if (dump.open(QIODevice::WriteOnly)) { dump.write(raw); dump.close(); }
                QFile wdump(QStringLiteral("dmghg_bad_wire_%1.bin").arg(attempt));
                if (wdump.open(QIODevice::WriteOnly)) { wdump.write(wire); wdump.close(); }
                if (attempt < 2) { postAttempt(path, body, cb, attempt + 1); return; }
                cb(false, {}, QStringLiteral("%1 响应解析失败(已重试)").arg(path));
                return;
            }
            const QJsonObject o = doc.object();
            const int code = o.value(QStringLiteral("code")).toInt();
            Logger::instance().info(QStringLiteral(
                "[DmghgV3] %1 code=%2 %3ms(首字节%4ms 读完%5ms) wire=%6B plain=%7B")
                .arg(path).arg(code).arg(started->elapsed()).arg(ttfb).arg(netMs)
                .arg(wire.size()).arg(raw.size()));
            const bool success = (code == 200 || code == 0);
            cb(success, o, success ? QString()
                                   : QStringLiteral("%1 (%2)")
                                         .arg(o.value(QStringLiteral("msg")).toString())
                                         .arg(code));
        });
    });
}

int DmghgClientV3::intern(const QString &newId) {
    const auto it = handleByNewId_.constFind(newId);
    if (it != handleByNewId_.constEnd()) return it.value();
    const int handle = nextHandle_++;
    handleByNewId_.insert(newId, handle);
    newIdByHandle_.insert(handle, newId);
    return handle;
}

int DmghgClientV3::handleForKey(const QString &key) {
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) return 0;
    bool oldId = false;
    const int asInt = trimmed.toInt(&oldId);
    // 能装进 int 的按旧 id 处理（videoDetail 会走 idMapping 换新 id）；
    // 18 位新编号超范围，直接登记一个会话句柄，反向表让 toNewId 直查命中。
    if (oldId && asInt > 0) return asInt;
    return intern(trimmed);
}

void DmghgClientV3::toNewId(int vid, const std::function<void(bool, const QString &, const QString &)> &cb) {
    const auto it = newIdByHandle_.constFind(vid);
    if (it != newIdByHandle_.constEnd()) { cb(true, it.value(), {}); return; }
    // 未登记的 int 视为旧整数 id，换一次映射并把该数值本身登记为句柄。
    post(QStringLiteral("/api/v1/app/client/video/idMapping"),
         QJsonObject{{QStringLiteral("oldIds"), QJsonArray{QString::number(vid)}}},
         [this, vid, cb](bool ok, const QJsonObject &root, const QString &err) {
             if (!ok) { cb(false, {}, err); return; }
             for (const QJsonValue &v : root.value(QStringLiteral("data")).toObject()
                                             .value(QStringLiteral("items")).toArray()) {
                 const QJsonObject o = v.toObject();
                 const QString newId = strF(o, "newId");
                 if (!newId.isEmpty()) {
                     handleByNewId_.insert(newId, vid);
                     newIdByHandle_.insert(vid, newId);
                     cb(true, newId, {});
                     return;
                 }
             }
             cb(false, {}, QStringLiteral("idMapping 没有 %1 的映射").arg(vid));
         });
}

void DmghgClientV3::search(const QString &keyword, int page, int limit,
                           const std::function<void(bool, const QVariantList &, const QString &)> &callback) {
    post(QStringLiteral("/api/v1/app/client/video/search"),
         QJsonObject{{QStringLiteral("keyword"), keyword},
                     {QStringLiteral("pageIndex"), page},
                     {QStringLiteral("pageSize"), limit}},
         [this, callback](bool ok, const QJsonObject &root, const QString &err) {
             if (!ok) { callback(false, {}, err); return; }
             QVariantList items = videoItems(root.value(QStringLiteral("data")).toObject()
                                                 .value(QStringLiteral("videos")).toArray());
             for (QVariant &v : items) {
                 QVariantMap m = v.toMap();
                 m.insert(QStringLiteral("vodId"), intern(m.value(QStringLiteral("vodId")).toString()));
                 v = m;
             }
             callback(true, items, {});
         });
}

void DmghgClientV3::listVideos(int channel, int page, int limit, const QString &sort, const QString &type,
                               const std::function<void(bool, const QVariantList &, int, const QString &)> &callback) {
    const QString tag = type.trimmed();
    auto request = [this, page, limit, sort, tag, callback](const QString &categoryId,
                                                            const QString &optionId) {
        QJsonObject body{{QStringLiteral("pageIndex"), page},
                         {QStringLiteral("pageSize"), limit},
                         {QStringLiteral("sort"), mapSort(sort)}};
        // 不传 categoryId 就是全站列表（实测 total=7108，日漫 3196 / 国漫 1085），
        // 这就是频道下拉第 0 项"全部频道"。
        if (!categoryId.isEmpty())
            body.insert(QStringLiteral("categoryId"), categoryId);
        if (!tag.isEmpty()) {
            if (optionId.isEmpty()) {
                callback(false, {}, 0, QStringLiteral("新协议里没有类型「%1」").arg(tag));
                return;
            }
            // 契约来自 APK 的 CategoryVideoListRequest：filters 是 [{dimensionCode, optionId}]。
            body.insert(QStringLiteral("filters"), QJsonArray{QJsonObject{
                {QStringLiteral("dimensionCode"), QStringLiteral("category")},
                {QStringLiteral("optionId"), optionId}}});
        }
        post(QStringLiteral("/api/v1/app/client/video/categoryList"), body,
             [this, callback](bool ok, const QJsonObject &root, const QString &err) {
                 if (!ok) { callback(false, {}, 0, err); return; }
                 const QJsonObject d = root.value(QStringLiteral("data")).toObject();
                 const int total = d.value(QStringLiteral("pageinfo")).toObject()
                                       .value(QStringLiteral("total")).toInt();
                 QVariantList items = videoItems(d.value(QStringLiteral("videos")).toArray());
                 for (QVariant &v : items) {
                     QVariantMap m = v.toMap();
                     m.insert(QStringLiteral("vodId"), intern(m.value(QStringLiteral("vodId")).toString()));
                     v = m;
                 }
                 callback(true, items, total, {});
             });
    };
    // channel 是频道下拉里的序号（分类 id 是不透明雪花号，界面只能按下标选）：
    // 0 = 全部频道，1..n 对应 listActive 返回的根分类。
    auto pickChannel = [this, channel, tag, callback, request]() {
        const QString categoryId = channel <= 0 || categories_.isEmpty()
            ? QString()
            : categories_.at(qBound(0, channel - 1, categories_.size() - 1))
                  .toMap().value(QStringLiteral("id")).toString();
        if (channel > 0 && categoryId.isEmpty()) {
            callback(false, {}, 0, QStringLiteral("新协议列表没有可用频道（type/listActive 未返回）"));
            return;
        }
        if (tag.isEmpty()) { request(categoryId, QString()); return; }
        loadTypeTags([this, tag, categoryId, request]() {
            request(categoryId, typeTagIds_.value(tag));
        });
    };
    // "全部"不需要分类表，省掉首屏那次 listActive。
    if (channel <= 0) { pickChannel(); return; }
    loadCategories(pickChannel);
}

void DmghgClientV3::loadCategories(const std::function<void()> &done) {
    if (!categories_.isEmpty() || !categoriesError_.isEmpty()) { done(); return; }
    categoryWaiters_.append(done);
    if (categoriesLoading_) return;
    categoriesLoading_ = true;
    // type/listActive 是分页接口（实测 total=263 / pages=27，默认一页仅 10 条），
    // 只读第 1 页就会"只剩 2 个频道"。带上 parentId="0" 让服务端只回根分类
    // （实测 total=10 / 9 条，几百字节；不过滤则要拉 106KB 全量），再按 pages 续拉兜底。
    auto pageIndex = std::make_shared<int>(1);
    auto pageCount = std::make_shared<int>(1);
    auto found = std::make_shared<QVector<QPair<int, QVariantMap>>>();
    auto seen = std::make_shared<QSet<QString>>();
    auto drain = [this]() {
        QVector<std::function<void()>> waiters;
        waiters.swap(categoryWaiters_);
        for (auto &w : waiters) w();
    };
    auto fetch = std::make_shared<std::function<void()>>();
    *fetch = [this, pageIndex, pageCount, found, seen, drain, fetch]() {
        post(QStringLiteral("/api/v1/app/client/type/listActive"),
             QJsonObject{{QStringLiteral("parentId"), QStringLiteral("0")},
                         {QStringLiteral("pageIndex"), *pageIndex},
                         {QStringLiteral("pageSize"), 50}},
             [this, pageIndex, pageCount, found, seen, drain, fetch](bool ok, const QJsonObject &root,
                                                                     const QString &err) {
                 if (!ok) {
                     categoriesError_ = err;
                     categoriesLoading_ = false;
                     drain();
                     return;
                 }
                 const QJsonObject d = root.value(QStringLiteral("data")).toObject();
                 for (const QJsonValue &v : d.value(QStringLiteral("types")).toArray()) {
                     const QJsonObject o = v.toObject();
                     // 顶层频道的 parentId 是 "0"，其 id 才能喂给 categoryList；
                     // 其余行是频道下的子分类，不能当频道用。
                     if (strF(o, "parentId") != QLatin1String("0")) continue;
                     const QString id = strF(o, "id");
                     if (id.isEmpty() || seen->contains(id)) continue;
                     seen->insert(id);
                     found->append({o.value(QStringLiteral("sortOrder")).toInt(),
                                    QVariantMap{{QStringLiteral("id"), id},
                                                {QStringLiteral("name"), strF(o, "name")}}});
                 }
                 const int pages = d.value(QStringLiteral("pageinfo")).toObject()
                                       .value(QStringLiteral("pages")).toInt();
                 if (pages > 0) *pageCount = pages;
                 if (*pageIndex < *pageCount) { ++*pageIndex; (*fetch)(); return; }
                 std::sort(found->begin(), found->end(),
                           [](const QPair<int, QVariantMap> &a, const QPair<int, QVariantMap> &b) {
                               return a.first < b.first;
                           });
                 for (const auto &entry : std::as_const(*found)) categories_.append(entry.second);
                 categoriesLoading_ = false;
                 drain();
             });
    };
    (*fetch)();
}

void DmghgClientV3::loadTypeTags(const std::function<void()> &done) {
    if (!typeTags_.isEmpty() || !typeTagsError_.isEmpty()) { done(); return; }
    typeTagWaiters_.append(done);
    if (typeTagsLoading_) return;
    typeTagsLoading_ = true;
    post(QStringLiteral("/api/v1/app/client/filter/dimensions"), QJsonObject{},
         [this](bool ok, const QJsonObject &root, const QString &err) {
             typeTagsLoading_ = false;
             if (!ok) {
                 typeTagsError_ = err;
             } else {
                 for (const QJsonValue &dv : root.value(QStringLiteral("data")).toObject()
                                                 .value(QStringLiteral("dimensions")).toArray()) {
                     const QJsonObject dim = dv.toObject();
                     if (strF(dim, "code") != QLatin1String("category")) continue;
                     for (const QJsonValue &ov : dim.value(QStringLiteral("options")).toArray()) {
                         const QJsonObject o = ov.toObject();
                         const QString name = strF(o, "name");
                         const QString id = strF(o, "id");
                         if (name.isEmpty() || id.isEmpty()) continue;
                         typeTagIds_.insert(name, id);
                         typeTags_.append(QVariantMap{{QStringLiteral("id"), id},
                                                      {QStringLiteral("name"), name}});
                     }
                 }
             }
             QVector<std::function<void()>> waiters;
             waiters.swap(typeTagWaiters_);
             for (auto &w : waiters) w();
         });
}

void DmghgClientV3::loadChannelOptions(const FilterCb &callback) {
    loadCategories([this, callback]() {
        callback(!categories_.isEmpty(), categories_, categoriesError_);
    });
}

void DmghgClientV3::loadTypeOptions(const FilterCb &callback) {
    loadTypeTags([this, callback]() {
        callback(!typeTags_.isEmpty(), typeTags_, typeTagsError_);
    });
}

void DmghgClientV3::loadWeeklySchedule(
        const std::function<void(bool, const QVariantMap &, const QString &)> &callback) {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!cachedSchedule_.isEmpty() && (nowMs - scheduleCachedAtMs_ < 15 * 60 * 1000)) {
        if (callback) callback(true, cachedSchedule_, {});
        return;
    }
    if (callback) scheduleWaiters_.append(callback);
    if (scheduleLoading_) return;
    scheduleLoading_ = true;

    auto drain = [this](bool ok, const QVariantMap &days, const QString &err) {
        scheduleLoading_ = false;
        QVector<std::function<void(bool, const QVariantMap &, const QString &)>> waiters;
        waiters.swap(scheduleWaiters_);
        for (auto &w : waiters) w(ok, days, err);
    };

    loadCategories([this, drain, nowMs]() {
        QStringList targetCategoryIds;
        for (const QVariant &cv : categories_) {
            const QVariantMap cm = cv.toMap();
            const QString name = cm.value(QStringLiteral("name")).toString();
            if (name.contains(QStringLiteral("日漫")) || name.contains(QStringLiteral("国漫"))) {
                targetCategoryIds.append(cm.value(QStringLiteral("id")).toString());
            }
        }
        if (targetCategoryIds.isEmpty()) {
            if (!categories_.isEmpty()) {
                targetCategoryIds.append(categories_.at(0).toMap().value(QStringLiteral("id")).toString());
                if (categories_.size() > 1)
                    targetCategoryIds.append(categories_.at(1).toMap().value(QStringLiteral("id")).toString());
            } else {
                targetCategoryIds.append(QString());
            }
        }

        struct FetchTask {
            QString categoryId;
            int page;
        };
        QVector<FetchTask> tasks;
        for (const QString &catId : targetCategoryIds) {
            tasks.append({catId, 1});
            tasks.append({catId, 2});
        }

        auto remaining = std::make_shared<int>(tasks.size());
        auto allVideos = std::make_shared<QVariantList>();
        auto hasError = std::make_shared<QString>();

        for (const auto &task : tasks) {
            QJsonObject body{{QStringLiteral("pageIndex"), task.page},
                             {QStringLiteral("pageSize"), 50},
                             {QStringLiteral("sort"), QStringLiteral("hottest")}};
            if (!task.categoryId.isEmpty())
                body.insert(QStringLiteral("categoryId"), task.categoryId);

            post(QStringLiteral("/api/v1/app/client/video/categoryList"), body,
                 [this, drain, nowMs, remaining, allVideos, hasError](bool ok, const QJsonObject &root, const QString &err) {
                     if (ok) {
                         const QJsonObject d = root.value(QStringLiteral("data")).toObject();
                         QVariantList items = videoItems(d.value(QStringLiteral("videos")).toArray());
                         allVideos->append(items);
                     } else {
                         if (hasError->isEmpty()) *hasError = err;
                     }
                     if (--(*remaining) == 0) {
                         if (allVideos->isEmpty() && !hasError->isEmpty()) {
                             drain(false, {}, *hasError);
                             return;
                         }
                         // 按星期分组 1..7 (周一..周日)
                         QVariantMap daysMap;
                         for (int d = 1; d <= 7; ++d) {
                             daysMap.insert(QString::number(d), QVariantList{});
                         }
                         QSet<QString> seenIds;
                         for (const QVariant &v : *allVideos) {
                             const QVariantMap m = v.toMap();
                             const QString rawId = m.value(QStringLiteral("vodId")).toString();
                             const QString sched = m.value(QStringLiteral("schedule")).toString().trimmed();
                             if (rawId.isEmpty() || sched.isEmpty()) continue;
                             const QVector<int> weekdays = parseScheduleWeekdays(sched);
                             for (int wd : weekdays) {
                                 if (wd < 1 || wd > 7) continue;
                                 const QString key = QString::number(wd);
                                 QVariantList list = daysMap.value(key).toList();
                                 if (list.size() >= 30) continue;
                                 const QString dedupKey = QStringLiteral("%1:%2").arg(wd).arg(rawId);
                                 if (seenIds.contains(dedupKey)) continue;
                                 seenIds.insert(dedupKey);

                                 QVariantMap card;
                                 card.insert(QStringLiteral("vodId"), intern(rawId));
                                 card.insert(QStringLiteral("vodName"), m.value(QStringLiteral("vodName")));
                                 card.insert(QStringLiteral("vodPic"), m.value(QStringLiteral("vodPic")));
                                 card.insert(QStringLiteral("vodRemarks"), m.value(QStringLiteral("vodRemarks")));
                                 card.insert(QStringLiteral("schedule"), sched);
                                 card.insert(QStringLiteral("typeName"), m.value(QStringLiteral("typeName")));
                                 card.insert(QStringLiteral("vodYear"), m.value(QStringLiteral("vodYear")));
                                 card.insert(QStringLiteral("vodArea"), m.value(QStringLiteral("vodArea")));
                                 list.append(card);
                                 daysMap.insert(key, list);
                             }
                         }
                         cachedSchedule_ = daysMap;
                         scheduleCachedAtMs_ = nowMs;
                         drain(true, daysMap, {});
                     }
                 });
        }
    });
}

void DmghgClientV3::fillEpisodes(const QString &vid, const QVariantList &playSources,
                                 const std::function<void(bool, const QVariantMap &, const QString &)> &cb,
                                 const QVariantMap &base) {
    // 各播放源的分集表互不依赖：逐个串行取是 N 个来回，这里一次并发、回齐再回调。
    auto pending = std::make_shared<int>(playSources.size());
    auto byIndex = std::make_shared<QVector<QVariantList>>(playSources.size());
    auto finish = [this, vid, playSources, byIndex, cb, base]() {
        QVariantList filled = playSources;
        for (int j = 0; j < filled.size(); ++j) {
            QVariantMap s = filled.at(j).toMap();
            const QVariantList names = byIndex->at(j);
            s.insert(QStringLiteral("episodes"), names);
            filled[j] = s;
            if (!names.isEmpty())
                episodeNames_.insert(vid + QLatin1Char('|')
                    + s.value(QStringLiteral("name")).toString(), names);
        }
        QVariantMap done = base;
        done.insert(QStringLiteral("playSources"), filled);
        cb(true, done, {});
    };
    for (int i = 0; i < playSources.size(); ++i) {
        const QString code = playSources.at(i).toMap().value(QStringLiteral("name")).toString();
        post(QStringLiteral("/api/v1/app/client/resolve/episodes"),
             QJsonObject{{QStringLiteral("videoId"), vid}, {QStringLiteral("code"), code}},
             [this, i, byIndex, pending, finish](bool ok, const QJsonObject &root, const QString &) {
                 if (ok) {
                     QVariantList &names = (*byIndex)[i];
                     for (const QJsonValue &v : root.value(QStringLiteral("data")).toObject()
                                                    .value(QStringLiteral("episodes")).toArray()) {
                         const QJsonObject eo = v.toObject();
                         names.append(strF(eo, "displayName").isEmpty() ? strF(eo, "episodeName")
                                                                        : strF(eo, "displayName"));
                     }
                 }
                 if (--(*pending) == 0) finish();
             });
    }
}

void DmghgClientV3::videoDetail(int vid,
                                const std::function<void(bool, const QVariantMap &, const QString &)> &callback) {
    toNewId(vid, [this, vid, callback](bool ok, const QString &newId, const QString &err) {
        if (!ok) { callback(false, {}, err); return; }
        post(QStringLiteral("/api/v1/app/client/video/detail"),
             QJsonObject{{QStringLiteral("videoId"), newId}},
             [this, vid, newId, callback](bool ok2, const QJsonObject &root, const QString &err2) {
                 if (!ok2) { callback(false, {}, err2); return; }
                 const QJsonObject v = root.value(QStringLiteral("data")).toObject()
                                           .value(QStringLiteral("video")).toObject();
                 QVariantMap m;
                 m.insert(QStringLiteral("vodId"), vid);
                 m.insert(QStringLiteral("vodIdV3"), strF(v, "id"));
                 m.insert(QStringLiteral("vodName"), MediaUtils::sanitizeUserText(strF(v, "title")));
                 m.insert(QStringLiteral("vodPic"), strF(v, "coverUrl"));
                 m.insert(QStringLiteral("vodYear"), strF(v, "year"));
                 m.insert(QStringLiteral("vodArea"), strF(v, "area"));
                 m.insert(QStringLiteral("typeName"),
                          strF(v, "categoryName").isEmpty() ? strF(v, "types") : strF(v, "categoryName"));
                 m.insert(QStringLiteral("vodActor"), strF(v, "actors"));
                 m.insert(QStringLiteral("vodDirector"), strF(v, "director"));
                 m.insert(QStringLiteral("vodContent"), MediaUtils::sanitizeUserText(strF(v, "description")));
                 m.insert(QStringLiteral("vodScore"), strF(v, "score"));
                 const int cur = v.value(QStringLiteral("currentEpisodes")).toInt();
                 const int total = v.value(QLatin1String("totalEpisodes")).toInt();
                 m.insert(QStringLiteral("vodRemarks"),
                          v.value(QStringLiteral("isFinished")).toInt()
                              ? QStringLiteral("%1集全").arg(total)
                              : (cur > 0 ? QStringLiteral("更新至%1集").arg(cur) : QString()));

                 // name 就是回传给 playVideo 的 playSource，必须能唯一还原 playerCode。
                 QVariantList playSources;
                 codeByName_.clear();
                 const QJsonArray players = v.value(QStringLiteral("player")).toArray();
                 for (const QJsonValue &pv : players) {
                     const QJsonObject po = pv.toObject();
                     QString code = strF(po, "playerCode").isEmpty() ? strF(v, "playerName")
                                                                     : strF(po, "playerCode");
                     if (code.isEmpty()) code = QStringLiteral("cn");
                     QVariantMap src;
                     src.insert(QStringLiteral("name"), code);
                     src.insert(QStringLiteral("title"),
                                strF(po, "playerName").isEmpty() ? code : strF(po, "playerName"));
                     src.insert(QStringLiteral("isVip"), po.value(QStringLiteral("isVip")).toInt());
                     src.insert(QStringLiteral("episodes"), QVariantList{});
                     playSources.append(src);
                     codeByName_.insert(code, code);
                 }
                 if (playSources.isEmpty()) {
                     QVariantMap src;
                     src.insert(QStringLiteral("name"), QStringLiteral("cn"));
                     src.insert(QStringLiteral("title"), QStringLiteral("默认线路"));
                     src.insert(QStringLiteral("episodes"), QVariantList{});
                     playSources.append(src);
                     codeByName_.insert(QStringLiteral("cn"), QStringLiteral("cn"));
                 }
                 currentVid_ = newId;
                 currentCode_ = playSources.first().toMap().value(QStringLiteral("name")).toString();
                 fillEpisodes(newId, playSources, callback, m);
             });
    });
}

void DmghgClientV3::playVideo(int vid, const QString &part, const QString &playSource,
                              const std::function<void(bool, const QString &, const QString &)> &callback) {
    toNewId(vid, [this, vid, part, playSource, callback](bool ok, const QString &newId, const QString &err) {
        if (!ok) { callback(false, {}, err); return; }
        const QString code = codeByName_.value(playSource,
                                               playSource.isEmpty() ? QStringLiteral("cn") : playSource);

        auto continueWithEpisodeNum = [this, newId, code, callback](int episodeNum) {
            currentVid_ = newId;
            currentEpisode_ = episodeNum;
            currentCode_ = code;

            post(QStringLiteral("/api/v1/app/client/resolve/qualities"),
                 QJsonObject{{QStringLiteral("videoId"), newId},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("episodeNum"), episodeNum},
                             {QStringLiteral("caps"), QJsonArray{}}},
                 [this, newId, code, episodeNum, callback](bool ok2, const QJsonObject &root,
                                                           const QString &err2) {
                     if (!ok2) { callback(false, {}, err2); return; }
                     const QJsonObject d = root.value(QStringLiteral("data")).toObject();
                     const QJsonArray qs = d.value(QStringLiteral("qualities")).toArray();
                     QStringList ladder;
                     ladder.reserve(qs.size());
                     for (const QJsonValue &qv : qs) {
                         const QJsonObject o = qv.toObject();
                         ladder << (strF(o, "quality")
                             + (o.value(QStringLiteral("isVipOnly")).toBool() ? QStringLiteral("(VIP)")
                                                                              : QString()));
                     }
                     QString pick;
                     for (const QJsonValue &qv : qs) {
                         const QJsonObject o = qv.toObject();
                         if (o.value(QStringLiteral("isVipOnly")).toBool()) continue;
                         pick = strF(o, "quality");
                         break;
                     }
                     bool forced = pick.isEmpty();
                     if (forced) pick = QStringLiteral("1080p");
                     Logger::instance().info(QStringLiteral("[Video] 清晰度档位 %1 → 取 %2%3")
                         .arg(ladder.join(QStringLiteral("/")), pick,
                              forced ? QStringLiteral("（无免费档，强制）") : QString()));

                     // 候选清晰度梯队（当被4032会员拒绝时依次降级重试）
                     QStringList candidates;
                     if (!pick.isEmpty()) candidates.append(pick);
                     for (const QString &q : {QStringLiteral("1080p"), QStringLiteral("720p"), QStringLiteral("480p"), QStringLiteral("360p"), QStringLiteral("auto")}) {
                         if (!candidates.contains(q))
                             candidates.append(q);
                     }

                     const QString exchangeToken = d.value(QStringLiteral("exchangeToken")).toString();
                     auto attemptIdx = std::make_shared<int>(0);
                     auto tryExchange = std::make_shared<std::function<void()>>();
                     *tryExchange = [this, tryExchange, newId, code, episodeNum, exchangeToken, candidates, attemptIdx, callback]() {
                         if (*attemptIdx >= candidates.size()) {
                             callback(false, {}, QStringLiteral("所有清晰度换取均失败 (4032会员限制)"));
                             return;
                         }
                         const QString quality = candidates.at(*attemptIdx);
                         ++(*attemptIdx);
                         post(QStringLiteral("/api/v1/app/client/resolve/exchange"),
                              QJsonObject{{QStringLiteral("videoId"), newId},
                                          {QStringLiteral("code"), code},
                                          {QStringLiteral("episodeNum"), episodeNum},
                                          {QStringLiteral("quality"), quality},
                                          {QStringLiteral("exchangeToken"), exchangeToken},
                                          {QStringLiteral("caps"), QJsonArray{}}},
                              [tryExchange, quality, callback](bool ok3, const QJsonObject &root2,
                                                               const QString &err3) {
                                  if (!ok3 && err3.contains(QLatin1String("4032"))) {
                                      Logger::instance().info(
                                          QStringLiteral("[Video] %1 换取被拒(4032)，尝试降级清晰度")
                                              .arg(quality));
                                      (*tryExchange)();
                                      return;
                                  }
                                  if (!ok3) { callback(false, {}, err3); return; }
                                  const QJsonObject dd = root2.value(QStringLiteral("data")).toObject();
                                  QString source = strF(dd, "originalUrl");
                                  if (source.isEmpty()) source = strF(dd, "playUrl");
                                  if (source.isEmpty()) {
                                      callback(false, {}, QStringLiteral("exchange 未返回播放标识 (%1)").arg(quality));
                                      return;
                                  }
                                  callback(true, source, {});
                              });
                     };
                     (*tryExchange)();
                 });
        };

        const QString key = newId + QLatin1Char('|') + code;
        if (episodeNames_.contains(key) && !episodeNames_.value(key).isEmpty()) {
            const QVariantList names = episodeNames_.value(key);
            int episodeNum = names.indexOf(part) + 1;
            if (episodeNum <= 0) episodeNum = trailingNumber(part);
            if (episodeNum <= 0) {
                callback(false, {}, QStringLiteral("无法把 \"%1\" 解析成集号").arg(part));
                return;
            }
            continueWithEpisodeNum(episodeNum);
        } else {
            post(QStringLiteral("/api/v1/app/client/resolve/episodes"),
                 QJsonObject{{QStringLiteral("videoId"), newId}, {QStringLiteral("code"), code}},
                 [this, key, part, continueWithEpisodeNum, callback](bool okEps, const QJsonObject &rootEps, const QString &) {
                     int episodeNum = 0;
                     if (okEps) {
                         QVariantList names;
                         for (const QJsonValue &v : rootEps.value(QStringLiteral("data")).toObject()
                                                        .value(QStringLiteral("episodes")).toArray()) {
                             const QJsonObject eo = v.toObject();
                             names.append(strF(eo, "displayName").isEmpty() ? strF(eo, "episodeName")
                                                                            : strF(eo, "displayName"));
                         }
                         if (!names.isEmpty()) {
                             episodeNames_.insert(key, names);
                             const int at = names.indexOf(part);
                             if (at >= 0) episodeNum = at + 1;
                         }
                     }
                     if (episodeNum <= 0) episodeNum = trailingNumber(part);
                     if (episodeNum <= 0) {
                         callback(false, {}, QStringLiteral("无法把 \"%1\" 解析成集号").arg(part));
                         return;
                     }
                     continueWithEpisodeNum(episodeNum);
                 });
        }
    });
}

void DmghgClientV3::parseSource(const QString &source,
                                const std::function<void(bool, const QVariantList &, const QString &)> &callback) {
    // jx 二次解析层新旧版本共用，直接复用基类实现（含多端点故障转移）。
    DmghgClient::parseSource(source, callback);
}

void DmghgClientV3::comments(int vid, int page, int limit,
                             const std::function<void(bool, const QVariantList &, int, const QString &)> &callback) {
    toNewId(vid, [this, vid, page, limit, callback](bool ok, const QString &newId, const QString &err) {
        if (!ok) { callback(false, {}, 0, err); return; }
        post(QStringLiteral("/api/v1/app/client/comment/list"),
             QJsonObject{{QStringLiteral("videoId"), newId},
                         {QStringLiteral("episodeNum"), currentEpisode_},
                         {QStringLiteral("pageIndex"), page},
                         {QStringLiteral("pageSize"), limit},
                         {QStringLiteral("sortBy"), QStringLiteral("hot")}},
             [callback](bool ok2, const QJsonObject &root, const QString &err2) {
                 if (!ok2) { callback(false, {}, 0, err2); return; }
                 const QJsonObject d = root.value(QStringLiteral("data")).toObject();
                 QVariantList out;
                 for (const QJsonValue &v : d.value(QStringLiteral("comments")).toArray()) {
                     const QJsonObject o = v.toObject();
                     QVariantMap m;
                     m.insert(QStringLiteral("id"), strF(o, "id"));
                     m.insert(QStringLiteral("uname"), MediaUtils::sanitizeUserText(strF(o, "userNickname")));
                     m.insert(QStringLiteral("comments"), MediaUtils::sanitizeUserText(strF(o, "content")));
                     m.insert(QStringLiteral("createdAt"),
                              QDateTime::fromMSecsSinceEpoch(
                                  qint64(o.value(QStringLiteral("createdAt")).toDouble()))
                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm")));
                     m.insert(QStringLiteral("hits"), o.value(QStringLiteral("likeCount")).toInt());
                     m.insert(QStringLiteral("isTop"), o.value(QStringLiteral("isTop")).toInt() != 0);
                     out.append(m);
                 }
                 const int total = d.value(QStringLiteral("pageinfo")).toObject()
                                       .value(QStringLiteral("total")).toInt();
                 callback(true, out, total, {});
             });
    });
}

void DmghgClientV3::createComment(const QString &comment, const QString &uuid, const QString &dots,
                                  const std::function<void(bool, const QString &)> &callback) {
    Q_UNUSED(uuid);
    Q_UNUSED(dots);
    // 新协议发表评论要求 videoId + episodeNum，旧签名里没有 —— 用最近一次播放的上下文。
    if (currentVid_.isEmpty()) {
        callback(false, QStringLiteral("新协议发评论需要先播放一集"));
        return;
    }
    post(QStringLiteral("/api/v1/app/client/comment/create"),
         QJsonObject{{QStringLiteral("videoId"), currentVid_},
                     {QStringLiteral("episodeNum"), currentEpisode_},
                     {QStringLiteral("content"), comment}},
         [callback](bool ok, const QJsonObject &, const QString &err) {
             callback(ok, ok ? QString() : err);
         });
}

void DmghgClientV3::danmaku(int vid, const QString &part, const QString &play, int startMs, int endMs,
                            const std::function<void(bool, const QVariantList &, int, const QString &)> &callback) {
    toNewId(vid, [this, vid, part, play, startMs, endMs, callback](bool ok, const QString &newId,
                                                                   const QString &err) {
        if (!ok) { callback(false, {}, 0, err); return; }
        const QString code = codeByName_.value(play, play.isEmpty() ? QStringLiteral("cn") : play);
        int episodeNum = 0;
        const QVariantList names = episodeNames_.value(newId + QLatin1Char('|') + code);
        const int at = names.indexOf(part);
        if (at >= 0) episodeNum = at + 1;
        if (episodeNum <= 0) episodeNum = trailingNumber(part);
        if (episodeNum <= 0) episodeNum = currentEpisode_;
        post(QStringLiteral("/api/v1/app/client/comment/danmaku"),
             QJsonObject{{QStringLiteral("videoId"), newId},
                         {QStringLiteral("episodeNum"), episodeNum},
                         {QStringLiteral("playerName"), code},
                         {QStringLiteral("timeFrom"), startMs},
                         {QStringLiteral("timeTo"), endMs}},
             [callback](bool ok2, const QJsonObject &root, const QString &err2) {
                 if (!ok2) { callback(false, {}, 0, err2); return; }
                 const QJsonObject d = root.value(QStringLiteral("data")).toObject();
                 const QJsonArray arr = d.value(QStringLiteral("list")).toArray();
                 QVariantList out;
                 out.reserve(arr.size());
                 for (const QJsonValue &v : arr) {
                     const QJsonObject o = v.toObject();
                     QVariantMap m;
                     m.insert(QStringLiteral("id"), strF(o, "id"));
                     m.insert(QStringLiteral("timePoint"), o.value(QStringLiteral("time")).toInt());
                     m.insert(QStringLiteral("text"), MediaUtils::sanitizeUserText(strF(o, "text")));
                     m.insert(QStringLiteral("color"), hexColorToArgb(strF(o, "color")));
                     m.insert(QStringLiteral("mode"), o.value(QStringLiteral("type")).toInt());
                     m.insert(QStringLiteral("size"),
                              fontSizeToName(o.value(QStringLiteral("fontSize")).toInt()));
                     out.append(m);
                 }
                 callback(true, out, d.value(QStringLiteral("total")).toInt(), {});
             });
    });
}
