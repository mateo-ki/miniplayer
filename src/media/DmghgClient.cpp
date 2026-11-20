#include "DmghgClient.h"

#include "media/DmghgSecrets.h"

#include "common/MediaUtils.h"
#include "infrastructure/Logger.h"

#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QList>
#include <QPair>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QCryptographicHash>
#include <QDateTime>
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

namespace {

// ── 主网关常量 (移植自 dmghg_api.py 与官方概念版 electron_bridge.dll) ───────────
// 官方业务域名
const QString kOfficialDomain = QStringLiteral("bkbfdm.hzhcbkj.cn");

// 官方主网关直连 CDN 节点 IP (Tencent EdgeOne，防 Fake-IP / DNS 污染 / 解析挂起)
const QStringList kFallbackGatewayIps = {
    QStringLiteral("124.225.27.84"),
    QStringLiteral("113.240.66.204"),
    QStringLiteral("124.72.128.203"),
    QStringLiteral("175.12.123.102"),
    QStringLiteral("171.108.209.133"),
    QStringLiteral("171.108.214.190"),
    QStringLiteral("171.108.213.42"),
    QStringLiteral("27.159.69.232"),
};

// 动态通过 DoH 解析到的最新 CDN IP
QStringList gDynamicCdnIps;

// 上次成功的网关域名/IP(进程内)。
QString gPreferredMainHost;

bool isIpv4Address(const QString &host) {
    static const QRegularExpression re(QStringLiteral("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$"));
    return re.match(host).hasMatch();
}

// 直连 IP 时返回逻辑官方域名, 保证 Tencent EdgeOne 与后端鉴权 requrl 正确
QString getHostDomain(const QString &hostUrl) {
    const QUrl u(hostUrl);
    const QString h = u.host();
    if (h.isEmpty() || isIpv4Address(h)) {
        return kOfficialDomain;
    }
    return h;
}

QStringList loadPersistedGatewayHosts() {
    const QSettings settings(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    QStringList hosts;
    for (const QString &host : settings.value(QStringLiteral("dmghg/discoveredHosts")).toStringList()) {
        const QString trimmed = host.trimmed();
        // 过滤已废弃不可用的 dokiapp.tech
        if (!trimmed.isEmpty() && !trimmed.contains(QStringLiteral("dokiapp.tech"), Qt::CaseInsensitive))
            hosts.append(trimmed);
    }
    return hosts;
}

void persistGatewayHost(const QString &host) {
    if (host.isEmpty() || host.contains(QStringLiteral("dokiapp.tech"), Qt::CaseInsensitive))
        return;
    QSettings settings(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
    QStringList hosts = settings.value(QStringLiteral("dmghg/discoveredHosts")).toStringList();
    if (hosts.contains(host))
        return;
    hosts.prepend(host);
    while (hosts.size() > 8)
        hosts.removeLast();
    settings.setValue(QStringLiteral("dmghg/discoveredHosts"), hosts);
}

// 候选网关: 上次成功域名优先, 其次引导发现的域名, 最后官方域名。
QStringList gatewayCandidates() {
    QStringList out;
    const auto add = [&out](const QString &host) {
        // EdgeOne 要求 Host 为官方域名。QNetworkAccessManager 不能可靠地
        // 在按 IP 请求时改写 Host, 直连 IP 会被网关返回 418, 因此不作为候选。
        const QUrl url(host);
        if (url.host().isEmpty() || isIpv4Address(url.host()))
            return;
        if (!host.isEmpty() && !out.contains(host))
            out.append(host);
    };
    add(gPreferredMainHost);
    for (const QString &host : loadPersistedGatewayHosts())
        add(host);
    add(QStringLiteral("http://%1").arg(kOfficialDomain));
    return out;
}

QString currentMainHost() {
    const QStringList candidates = gatewayCandidates();
    return candidates.isEmpty() ? QStringLiteral("http://%1").arg(kOfficialDomain) : candidates.first();
}

// 2026-09-20 实测: 服务端不校验 appid 值(任意值均 20000, 仅作风控身份标签)。
// 使用与官方 app/PC 不同的自定义 appid, 把本客户端的风控状态与真实 app 隔离:
// 任一方触发 appid 级风控(实测为无效鉴权请求风暴所致, ~3 分钟自愈)时互不影响。
constexpr const char *kAppId = "6600000000000001";
constexpr const char *kXVersion = "2024-09-24";
constexpr const char *kSystem = "3";
constexpr const char *kProtocolVer = "1.0.0";
constexpr const char *kPlatform = "win32";
constexpr const char *kUserAgent = "okhttp/4.12.0";

// AES-256-CBC 鉴权密钥与 IV 派生掩码: 以 XOR 混淆形态内嵌 (DmghgSecrets),
// 二进制与源码不含明文, 防 strings 提取。
static const QByteArray &kAuthKey = DmghgSecrets::authKey();
static const QByteArray &kIvKey2 = DmghgSecrets::ivKey2();
// 桌面版协议版本 (IV 明文前缀, 由反解 base64 还原确认)。
constexpr const char *kIvProtocolVer = "1.4.4";

constexpr int kMainGatewayTimeoutMs = 4500;
constexpr int kSessionProbeTimeoutMs = 1500;
// 后台恢复扫描: 相邻时间桶探测之间的间隔, 降低对服务端的瞬时压力
// (抖动期密集短超时请求会反过来加重服务端限流)。
constexpr int kRecoveryStepDelayMs = 250;
// 后台恢复扫描的全局冷却: 一次恢复发起后, 此期间内不再发起新的恢复扫描。
// 抖动期多个失败请求各自触发完整 ±8 桶扫描会形成「挂了→疯狂扫描→更挂」的恶性循环,
// 冷却把恢复频率压到每 30s 至多一次, 让服务端抖动窗口自行过去。
constexpr int kRecoveryCooldownMs = 30000;
// 恢复扫描命中的时间桶偏移 (相对实时 unix 秒, 单位=10000s 桶)。0=与实时对齐。
// recoverSessionIv 命中后写入; sessionIv() 派生时把实时时间加上此偏移再 //10000。
std::atomic<int> gLiveBucketOffset{0};
// 本机时钟与网关服务器时间的偏差 (秒), 由 HTTP Date 响应头测得。
// 虚拟机时钟漂移是 403501 的常见根因: IV 按本机时间派生, 时钟偏差超过
// 服务器容差即被拒。恢复扫描的每个响应都会校对并自动修正。
std::atomic<qint64> gServerTimeSkewSec{0};

// 全局从任意 HTTP 响应的 Date 响应头自适应同步服务器时钟漂移, 避免 IV 跨桶失效
void syncServerTimeSkew(const QByteArray &dateHeader) {
    if (dateHeader.isEmpty()) return;
    const QDateTime serverDate = QDateTime::fromString(
        QString::fromLatin1(dateHeader), Qt::RFC2822Date);
    if (serverDate.isValid()) {
        const qint64 observedSkew = serverDate.toSecsSinceEpoch()
            - QDateTime::currentSecsSinceEpoch();
        if (qAbs(observedSkew - gServerTimeSkewSec.load()) > 5) {
            gServerTimeSkewSec.store(observedSkew);
            Logger::instance().info(QStringLiteral(
                "[Dmghg] synced server clock skew from Date header: %1s").arg(observedSkew));
        }
    }
}

qint64 serverNowMs() {
    return QDateTime::currentMSecsSinceEpoch() + gServerTimeSkewSec.load() * 1000;
}
// 上次恢复扫描发起的 unix 毫秒; 配合 kRecoveryCooldownMs 限流。
std::atomic<qint64> gLastRecoveryStartMs{0};

// 服务端 appid 级瞬时风控静默期 (2026-09-20 实测): IV 逐字节正确也会被间歇性
// 403501, 约 3 分钟自愈; 期间任何重试/扫描只会延长窗口。时钟正常时进入静默
// 等待, 到期单探测复查, 通过后重放排队请求; 仍未通过则按 60s 间隔继续复查。
constexpr int kRiskControlInitialWaitMs = 180000;
constexpr int kRiskControlRecheckDelayMs = 60000;
constexpr int kRiskControlMaxRechecks = 5;
std::atomic<qint64> gRiskControlUntilMs{0};

// 搜索独立网关 sign 密钥 (混淆内嵌)。
static const QByteArray kSearchSecretBa = DmghgSecrets::searchSecret();
constexpr const char *kSearchHost = "http://tx.xn--vhqr42drhf5k7b.com";

// ── jx 二次解析常量 (移植自 jx_parse.py) ──────────────
// 2026-09-20 实测: 当前官方端点为 jxbfdm.hzhcbkj.cn (app 抓包确认), 列首位;
// jxbfdm.xn--2qq276avrz.top 仍存活作备选; new.jx.dokiapp.tech DNS 已失效, 留末位兜底。
const QStringList kJxEndpoints = {
    QStringLiteral("http://jxbfdm.hzhcbkj.cn/?md5="),
    QStringLiteral("http://jxbfdm.xn--2qq276avrz.top/?md5="),
    QStringLiteral("http://new.jx.dokiapp.tech/?md5="),
};
std::atomic<int> gPreferredJxEndpoint{0};
static const QByteArray kJxAesKey = DmghgSecrets::jxAesKey();
static const QByteArray kJxAesIv = DmghgSecrets::jxAesIv();
static const QByteArray kJxAuthCfgKeyBa = DmghgSecrets::jxAuthCfgKey();
constexpr int kJxAuthExpire = 600;
static const QByteArray kJxHdrAuthKey = DmghgSecrets::jxHdrAuthKey();
static const QByteArray kJxHdrProofKey = DmghgSecrets::jxHdrProofKey();
static const QByteArray kJxHdrProbeKey = DmghgSecrets::jxHdrProbeKey();
static const QByteArray kJxHdrCheckKey = DmghgSecrets::jxHdrCheckKey();

const QStringList kAdDomains = {
    "v1-ad.video.yximgs.com",
    "v2-ad.video.yximgs.com",
    "v3-ad.video.yximgs.com",
};
const QStringList kSnsDomains = {
    "sns-video-bd.xhscdn.com",
    "sns-video-hw.xhscdn.com",
    "sns-video-hs.xhscdn.com",
};

QByteArray md5Hex(const QByteArray &data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex();
}

QString genNonce(int n) {
    static const char *kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    QString out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.append(QLatin1Char(kAlphabet[QRandomGenerator::global()->bounded(62)]));
    return out;
}

// 由 unix 秒直接派生会话 IV (确定性, 见文件头算法说明)。
QByteArray deriveLiveIv(qint64 unixSeconds) {
    const qint64 n = unixSeconds / 10000;
    const QByteArray plaintext = (QStringLiteral("%1-%2")
            .arg(QLatin1String(kIvProtocolVer)).arg(n)).toUtf8();
    const QByteArray b64 = plaintext.toBase64();
    QByteArray ivInput(16, '\0');
    const int m = qMin(16, b64.size());
    for (int i = 0; i < m; ++i)
        ivInput[i] = char(kIvKey2[i] ^ static_cast<uchar>(b64[i]));

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    QByteArray output(16, '\0');
    int outLen = 0;
    if (ctx
        && EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr,
                              reinterpret_cast<const unsigned char *>(kAuthKey.constData()), nullptr) == 1
        && EVP_CIPHER_CTX_set_padding(ctx, 0) == 1
        && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(output.data()), &outLen,
                             reinterpret_cast<const unsigned char *>(ivInput.constData()), ivInput.size()) == 1) {
        EVP_CIPHER_CTX_free(ctx);
        return output;
    }
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    return {};
}

// bool usingInjectedIv() {
//     return !injectedSessionIv().isNull();
// }

// 手工注入的真实会话 IV(32 位十六进制串)。优先级:环境变量
// DMGHG_SESSION_IV > QSettings dmghg/sessionIv。当服务端更新 IV 派生逻辑、
// 时间派生不再命中时,抓取真实 App 一次请求的 authentication 头
// (base64 解码取前 16 字节)填入即可恢复,无需重新编译或逆向新算法。
// 注入模式下 403501/30000 表示该 IV 本身失效,不再自动派生。
QByteArray injectedSessionIv() {
    static const QByteArray iv = [] {
        const QByteArray env = qgetenv("DMGHG_SESSION_IV").trimmed();
        if (!env.isEmpty()) {
            const QByteArray hex = QByteArray::fromHex(env);
            if (hex.size() == 16) return hex;
            Logger::instance().warn(QStringLiteral("[Dmghg] DMGHG_SESSION_IV invalid (need 32 hex): %1")
                .arg(QString::fromLatin1(env)));
        }
        QSettings settings(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
        const QString persisted = settings.value(QStringLiteral("dmghg/sessionIv")).toString().trimmed();
        if (!persisted.isEmpty()) {
            const QByteArray hex = QByteArray::fromHex(persisted.toLatin1());
            if (hex.size() == 16) {
                Logger::instance().info(QStringLiteral("[Dmghg] using injected session IV from settings"));
                return hex;
            }
        }
        return QByteArray();
    }();
    return iv;
}

// 会话 IV: 优先注入值, 否则按当前时间确定性派生。
// 派生用时间 = 实时 unix 秒 + gLiveBucketOffset*10000 (恢复扫描命中后会写入非 0 偏移,
// 用于修正本机时钟漂移; 正常情况下偏移为 0)。
QByteArray sessionIv() {
    const QByteArray injected = injectedSessionIv();
    if (!injected.isNull()) return injected;
    const qint64 t = QDateTime::currentSecsSinceEpoch()
        + gServerTimeSkewSec.load()
        + qint64(gLiveBucketOffset.load()) * 10000;
    return deriveLiveIv(t);
}

bool usingInjectedIv() {
    return !injectedSessionIv().isNull();
}

} // namespace

QString normalizePath(const QString &path) {
    QString p = path;
    while (p.startsWith(QLatin1Char('/'))) p = p.mid(1);
    while (p.endsWith(QLatin1Char('/'))) p.chop(1);
    return QLatin1Char('/') + p;
}

// AES-CBC 加密(PKCS7),返回原始密文字节。
// OpenSSL 默认执行一次 PKCS7 padding，调用方不应再次手动补齐。
QByteArray aesCbcEncryptRaw(const EVP_CIPHER *cipher, const QByteArray &key,
                            const QByteArray &iv, const QByteArray &plain) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    QByteArray result;
    if (ctx && EVP_EncryptInit_ex(ctx, cipher, nullptr,
            reinterpret_cast<const unsigned char *>(key.constData()),
            reinterpret_cast<const unsigned char *>(iv.constData())) == 1) {
        QByteArray out(plain.size() + EVP_CIPHER_block_size(cipher), '\0');
        int outlen = 0, finallen = 0;
        if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &outlen,
                reinterpret_cast<const unsigned char *>(plain.constData()), plain.size()) == 1
            && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + outlen,
                &finallen) == 1) {
            result = out.left(outlen + finallen);
        }
    }
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    return result;
}

// 鉴权明文在调用方已按协议完成 PKCS7 填充；这里禁止 OpenSSL 再补一块。
QByteArray aesCbcEncryptRawNoPadding(const EVP_CIPHER *cipher, const QByteArray &key,
                                     const QByteArray &iv, const QByteArray &plain) {
    if (plain.isEmpty() || (plain.size() % EVP_CIPHER_block_size(cipher)) != 0)
        return {};
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    QByteArray result;
    if (ctx && EVP_EncryptInit_ex(ctx, cipher, nullptr,
            reinterpret_cast<const unsigned char *>(key.constData()),
            reinterpret_cast<const unsigned char *>(iv.constData())) == 1
        && EVP_CIPHER_CTX_set_padding(ctx, 0) == 1) {
        QByteArray out(plain.size() + EVP_CIPHER_block_size(cipher), '\0');
        int outlen = 0;
        int finallen = 0;
        if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &outlen,
                reinterpret_cast<const unsigned char *>(plain.constData()), plain.size()) == 1
            && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + outlen,
                &finallen) == 1) {
            result = out.left(outlen + finallen);
        }
    }
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    return result;
}

// AES-CBC 加密,返回 base64(密文)。
QByteArray aesCbcEncryptBase64(const EVP_CIPHER *cipher, const QByteArray &key,
                               const QByteArray &iv, const QByteArray &plain) {
    return aesCbcEncryptRaw(cipher, key, iv, plain).toBase64();
}

// AES-CBC 解密(去 PKCS7),输入原始密文字节。
QByteArray aesCbcDecryptRaw(const EVP_CIPHER *cipher, const QByteArray &key,
                           const QByteArray &iv, const QByteArray &raw) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    QByteArray result;
    if (ctx && EVP_DecryptInit_ex(ctx, cipher, nullptr,
            reinterpret_cast<const unsigned char *>(key.constData()),
            reinterpret_cast<const unsigned char *>(iv.constData())) == 1) {
        QByteArray out(raw.size() + 16, '\0');
        int outlen = 0, finallen = 0;
        if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &outlen,
                reinterpret_cast<const unsigned char *>(raw.constData()), raw.size()) == 1
            && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + outlen,
                &finallen) == 1) {
            result = out.left(outlen + finallen);
        }
    }
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    return result;
}

// AES-CBC 解密,输入 base64 密文。
QByteArray aesCbcDecryptBase64(const EVP_CIPHER *cipher, const QByteArray &key,
                              const QByteArray &iv, const QByteArray &base64Data) {
    return aesCbcDecryptRaw(cipher, key, iv, QByteArray::fromBase64(base64Data));
}

// 内嵌 RSA-2048 私钥从磁盘加载(开发期落回已知路径,发布时随可执行文件部署)。
QByteArray loadRsaPem() {
    static QByteArray cached;
    static bool tried = false;
    if (tried) return cached;
    tried = true;
    // 首选: 编译期内嵌 (XOR 混淆) 的私钥, 磁盘与安装包均不落明文 pem。
    cached = DmghgSecrets::rsaPem();
    if (!cached.isEmpty()) {
        Logger::instance().info(QStringLiteral("[Dmghg] RSA key loaded from embedded obfuscated blob (%1 bytes)")
            .arg(cached.size()));
        return cached;
    }
    // 开发回退: 本地磁盘 pem。
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/dmghg_key.pem"),
        QStringLiteral("D:/project/ida/dm/embedded_key.pem"),
    };
    for (const QString &path : candidates) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            cached = f.readAll();
            Logger::instance().info(QStringLiteral("[Dmghg] RSA key loaded from %1 (%2 bytes)")
                .arg(path).arg(cached.size()));
            return cached;
        }
    }
    Logger::instance().error(QStringLiteral("[Dmghg] RSA private key not found"));
    return {};
}

// RSA-2048 PKCS1 v1.5 解密:256 字节密文块 -> 会话密钥明文。
QByteArray rsaPkcs1Decrypt(const QByteArray &block) {
    const QByteArray pem = loadRsaPem();
    if (pem.isEmpty() || block.size() != 256) return {};
    BIO *bio = BIO_new_mem_buf(pem.constData(), int(pem.size()));
    if (!bio) return {};
    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return {};

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    EVP_PKEY_free(pkey);
    if (!ctx) return {};
    QByteArray result;
    if (EVP_PKEY_decrypt_init(ctx) <= 0
        || EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return {};
    }
    unsigned char out[512] = {0};
    size_t outlen = sizeof(out);
    if (EVP_PKEY_decrypt(ctx, out, &outlen,
            reinterpret_cast<const unsigned char *>(block.constData()), block.size()) == 1) {
        result = QByteArray(reinterpret_cast<const char *>(out), int(outlen));
    }
    EVP_PKEY_CTX_free(ctx);
    return result;
}

// 生成 Authentication 头:base64(IV || AES-256-CBC(base64(plaintext)))。
QString makeAuth(qint64 tsMs) {
    const QString shortTs = QString::number(tsMs % 10000000);
    const QByteArray plain = QStringLiteral("%1-%2-%3-%4")
        .arg(shortTs, QLatin1String(kSystem), QLatin1String(kProtocolVer), QLatin1String(kPlatform))
        .toUtf8();
    QByteArray inner = plain.toBase64();
    const int pad = 16 - (inner.size() % 16);
    inner.append(QByteArray(pad, char(pad)));
    const QByteArray iv = sessionIv();
    const QByteArray ct = aesCbcEncryptRawNoPadding(EVP_aes_256_cbc(), kAuthKey, iv, inner);
    return QString::fromLatin1((iv + ct).toBase64());
}

QMap<QByteArray, QByteArray> buildMainGatewayHeaders(qint64 tsMs, const QString &host) {
    const QString tsStr = QString::number(tsMs);
    const QString domain = getHostDomain(host);
    const QString reqUrl = QStringLiteral("http://%1").arg(domain);
    return {
        {"Host", domain.toUtf8()},
        {"content-type", "application/json"},
        {"accept", "application/json"},
        {"x-version", kXVersion},
        {"appid", kAppId},
        {"system", kSystem},
        {"x-token", ""},
        {"requrl", reqUrl.toUtf8()},
        {"ts", tsStr.toUtf8()},
        {"authentication", makeAuth(tsMs).toUtf8()},
        {"user-agent", kUserAgent},
    };
}

// ── jx 三层 X-Goepp 头构造 ──────────────────────────────
QString buildClientHeader(const QString &path, const QString &md5Param, const QString &authParam,
                          const QString &key, const QStringList &bindValues,
                          const std::optional<int> &expireSeconds, qint64 now) {
    const QString nonce = genNonce(20);
    QStringList seed;
    seed << normalizePath(path) << md5Param << authParam;
    qint64 expireAt = -1;
    if (expireSeconds) {
        expireAt = now + *expireSeconds;
        seed << QString::number(expireAt);
    }
    for (const QString &v : bindValues) seed << v;
    seed << nonce << key;
    const QByteArray h = md5Hex(seed.join(QLatin1Char('|')).toUtf8());
    if (expireAt >= 0)
        return QStringLiteral("v1:%1:%2:%3").arg(expireAt).arg(nonce, QString::fromLatin1(h));
    return QStringLiteral("v1:%1:%2").arg(nonce, QString::fromLatin1(h));
}

QMap<QByteArray, QByteArray> buildApiClientHeaders(const QString &path, const QString &md5Param,
                                                    const QString &authParam, qint64 now) {
    const QString hAuth = buildClientHeader(path, md5Param, authParam, kJxHdrAuthKey, {}, 600, now);
    const QString hProof = buildClientHeader(path, md5Param, authParam, kJxHdrProofKey,
                                             {hAuth}, std::nullopt, now);
    const QString hCheck = buildClientHeader(path, md5Param, authParam, kJxHdrCheckKey,
                                             {hAuth, hProof}, std::nullopt, now);
    return {
        {"Accept", "*/*"},
        {"Connection", "keep-alive"},
        {"X-Goepp-Client-Auth", hAuth.toUtf8()},
        {"X-Goepp-Client-Proof", hProof.toUtf8()},
        {"X-Goepp-Client-Probe", kJxHdrProbeKey},
        {"X-Goepp-Client-Check", hCheck.toUtf8()},
    };
}

QString generateAuthParams(const QString &path, int expireTime, const QString &authKey, qint64 now) {
    const QString normPath = normalizePath(path);
    const qint64 expireAt = now + expireTime;
    const QString rand = genNonce(16);
    const QString uid = QStringLiteral("0");
    const QString signStr = QStringLiteral("%1-%2-%3-%4-%5")
        .arg(normPath).arg(expireAt).arg(rand).arg(uid).arg(authKey);
    const QByteArray md5hash = md5Hex(signStr.toUtf8());
    return QStringLiteral("sign=%1-%2-%3-%4")
        .arg(expireAt).arg(rand).arg(uid).arg(QString::fromLatin1(md5hash));
}

// encrypt_md5_id:source 标识加密成 api_source。
QString encryptMd5Id(const QString &md5Str) {
    if (md5Str.isEmpty()) return md5Str;
    const QString randomPrefix = QStringLiteral("%1")
        .arg(QRandomGenerator::global()->bounded(10000), 4, 10, QChar('0'));
    QString scrambled = md5Str.toLower();
    std::reverse(scrambled.begin(), scrambled.end());
    const QString guardSeed = QStringLiteral("%1:%2:%3:%4")
        .arg(randomPrefix).arg(scrambled)
        .arg(QString::fromLatin1(kJxAesKey)).arg(QString::fromLatin1(kJxAesIv));
    const QString guard = QString::fromLatin1(md5Hex(guardSeed.toUtf8())).left(2);
    const QString wrapped = scrambled.left(16) + guard + scrambled.mid(16);
    const QString dataToEncrypt = QStringLiteral("%1-%2").arg(randomPrefix, wrapped);
    const QByteArray enc = aesCbcEncryptBase64(EVP_aes_128_cbc(), kJxAesKey, kJxAesIv,
                                               dataToEncrypt.toUtf8());
    if (enc.size() % 4 == 0) return QString::fromLatin1(enc);
    return QString::fromLatin1(enc.toBase64());
}

std::pair<QString, QMap<QByteArray, QByteArray>> getApiUrl(const QString &source, qint64 now,
                                                           const QString &endpoint) {
    QString md5Source = source;
    if (md5Source.startsWith(QStringLiteral("new-"))) md5Source = md5Source.mid(4);
    const QString apiSource = encryptMd5Id(md5Source);
    const QString authParam = generateAuthParams(QStringLiteral("/"), kJxAuthExpire, kJxAuthCfgKeyBa, now);
    auto headers = buildApiClientHeaders(QStringLiteral("/"), apiSource, authParam, now);
    const QString url = QStringLiteral("%1%2&%3").arg(endpoint, apiSource, authParam);
    return {url, headers};
}

QString decryptUrlField(const QString &encrypted) {
    if (encrypted.isEmpty()) return encrypted;
    // 注意:此处 key=kJxAesIv,iv=kJxAesKey(与协议一致,顺序互换)。
    const QByteArray dec = aesCbcDecryptBase64(EVP_aes_128_cbc(), kJxAesIv, kJxAesKey, encrypted.toUtf8());
    if (dec.isEmpty()) return encrypted;
    return QString::fromUtf8(dec);
}

QString applyUrlRewrites(QString url, int idx) {
    // 2026-09-20: 最新 parse 脚本 (play 响应下发) 的改写目标已换为
    // m3bkbfdm.hzhcbkj.cn; 旧 xn--2qq276avrz.top 域名随时可能停用。
    url.replace(QStringLiteral("play.ddmm.hzhcbkj.cn"), QStringLiteral("m3bkbfdm.hzhcbkj.cn"));
    url.replace(QStringLiteral("new.ddmm.hzhcbkj.cn"), QStringLiteral("m3bkbfdm.hzhcbkj.cn"));
    url.replace(QRegularExpression(QStringLiteral("bdmov\\.a\\.yximgs\\.com")),
                QStringLiteral("v4-kling.kechuangai.com"));
    url.replace(QStringLiteral("hwmov6.a.yximgs.com"), QStringLiteral("v4-kling.kechuangai.com"));
    url.replace(QRegularExpression(QStringLiteral("v[123]-ad\\.video\\.yximgs\\.com")),
                kAdDomains.value(idx % 3));
    url.replace(QStringLiteral("sns-video-default.xhscdn.com"), kSnsDomains.value(idx % 3));
    return url;
}

QString getMediaType(const QString &url) {
    const QString u = url.toLower();
    if (u.contains(QStringLiteral(".mp4"))) return QStringLiteral("mp4");
    if (u.contains(QStringLiteral(".m3u8"))) return QStringLiteral("hls");
    if (u.contains(QStringLiteral(".flv"))) return QStringLiteral("flv");
    return QStringLiteral("multi");
}

QVariantMap processPlayAddr(const QJsonObject &pa, int idx) {
    QVariantMap result;
    if (!pa.contains(QStringLiteral("addr")) || !pa.contains(QStringLiteral("m3u8FileDomain")))
        return result;
    const QString addr = decryptUrlField(pa.value(QStringLiteral("addr")).toString());
    const QString domain = decryptUrlField(pa.value(QStringLiteral("m3u8FileDomain")).toString());
    const QString base = domain + addr;
    if (!base.startsWith(QStringLiteral("http://")) && !base.startsWith(QStringLiteral("https://")))
        return result;
    QString url = base;
    if (domain.contains(QStringLiteral("anixx.r2"))) {
        url = QStringLiteral("https://sns-music.xhscdn.com/104002e031m0qe7o84s0m6saf3o");
    } else {
        url = applyUrlRewrites(url, idx);
    }
    result.insert(QStringLiteral("url"), url);
    result.insert(QStringLiteral("type"), getMediaType(url));
    result.insert(QStringLiteral("name"),
                  QStringLiteral("%1 %2")
                      .arg(pa.value(QStringLiteral("desc")).toString(),
                           pa.value(QStringLiteral("title")).toString())
                      .trimmed());
    return result;
}

// 取 JSON 对象的字符串值,容忍 number/string 两种类型。
QString strField(const QJsonObject &o, const QString &key) {
    const QJsonValue v = o.value(key);
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toInt());
    return {};
}


DmghgClient::DmghgClient(QObject *parent)
    : QObject(parent) {
    // 异步查询官方 bootstrap 引导节点以获取最新主域名
    QTimer::singleShot(100, this, [this]() { queryBootstrapGateway(); });
    // 异步通过 DoH (阿里 DNS) 解析官方主域名的真实 CDN IP, 避免本地 DNS/Clash Fake-IP 挂起
    QTimer::singleShot(200, this, [this]() { resolveGatewayByDoh(kOfficialDomain); });
}

// 带重试的 GET:直连接口偶发超时/503,重试即恢复。
// attempt 以 shared_ptr 持有,确保异步回复回调触发时函数对象仍存活。
void fetchWithRetry(QNetworkAccessManager &nam, const QNetworkRequest &req,
                    const QMap<QByteArray, QByteArray> &headers, int maxAttempts, int delayMs,
                    const std::function<void(bool, const QByteArray &, const QString &)> &onDone) {
    auto attempt = std::make_shared<std::function<void(int)>>();
    *attempt = [&nam, req, headers, maxAttempts, delayMs, onDone,
                attempt](int n) {
        QNetworkRequest r = req;
        for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
            r.setRawHeader(it.key(), it.value());
        QNetworkReply *reply = nam.get(r);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, n, maxAttempts, delayMs, onDone, attempt]() {
            syncServerTimeSkew(reply->rawHeader("Date"));
            const QNetworkReply::NetworkError err = reply->error();
            const QString errStr = reply->errorString();
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            reply->deleteLater();
            if (err != QNetworkReply::NoError) {
                Logger::instance().warn(QStringLiteral("[Dmghg] reply error: %1 (%2), httpStatus=%3, bodyLen=%4")
                    .arg(int(err)).arg(errStr).arg(httpStatus).arg(body.size()));
            }
            // 传输错误或空响应且仍可重试时延迟重试。
            if ((err != QNetworkReply::NoError || body.isEmpty()) && n < maxAttempts) {
                QTimer::singleShot(delayMs, [attempt, n]() { (*attempt)(n + 1); });
                return;
            }
            if (err != QNetworkReply::NoError && body.isEmpty()) {
                const QString message = err == QNetworkReply::RemoteHostClosedError
                    ? QStringLiteral("解析服务器暂时断开连接,请稍后重试")
                    : QStringLiteral("网络请求失败(%1)").arg(int(err));
                onDone(false, {}, message);
                return;
            }
            onDone(true, body, {});
        });
    };
    (*attempt)(1);
}

// 403501(签名校验失败)/30000(解码异常) 在新算法下本不该出现 (IV 纯时间派生, 桌面版实抓逐字节
// 与公式一致). 实测同一正确时间桶的同一 IV, 服务器会间歇性返回 403501 后又放行 —— 2026-09-20
// 确认为服务端 appid 级瞬时风控 (重放已用 nonce 会触发, 约 3 分钟自愈), 而非算法/时钟问题.
// 因此先对当前桶静默重试若干次再判定:
//   注入 IV 命中 403501/30000 -> 注入值本身失效, 直接引导重新抓取, 不重试;
//   派生 IV 重试耗尽仍 403501/30000 -> 不做主机故障转移 (CDN 后同一后端, 重发只延长风控),
//     交 recoverSessionIv: Date 校准时钟 + ±1 桶探测, 均未命中则判为风控进入静默期.
// 每次重试用新 ts 重新派生 IV: 若期间越过 10000s 桶边界, IV 自然更新, 兼顾时钟漂移情形.
constexpr int kSigRetryAttempts = 3;   // 含首次, 共 3 次 (2 次静默重试)
constexpr int kSigRetryDelayMs = 800;

void DmghgClient::callMainGateway(const QString &route, qint64 nowMs,
        const std::function<void(bool, const QByteArray &, const QString &)> &callback) {
    Q_UNUSED(nowMs);  // IV 现按当前实时时间派生, 调用方传入的 nowMs 仅保留签名兼容。
    // 风控静默期: 已判定为服务端 appid 级风控(非时钟/桶偏移问题)期间不再发任何
    // 主网关请求 —— 重试只会延长窗口。请求入队, 静默期后的复查通过即自动重放。
    if (QDateTime::currentMSecsSinceEpoch() < gRiskControlUntilMs.load()) {
        if (pendingSessionReplays_.size() < 8)
            pendingSessionReplays_.append({route, callback});
        Logger::instance().info(QStringLiteral(
            "[Dmghg] main gateway silent for server risk control (route=%1), queued for auto replay")
            .arg(route));
        callback(false, {}, QStringLiteral("服务端风控静默期中,稍后自动重试"));
        return;
    }
    const bool injected = usingInjectedIv();
    const QStringList hosts = gatewayCandidates();
    auto attempt = std::make_shared<std::function<void(int, int)>>();
    *attempt = [this, route, callback, injected, attempt, hosts](int n, int hostIdx) {
        const QString host = hosts.at(hostIdx);
        QUrl url(QStringLiteral("%1/%2").arg(host, route));
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(kMainGatewayTimeoutMs);
        fetchWithRetry(nam_, req, buildMainGatewayHeaders(serverNowMs(), host),
            1, 0,
            [this, route, callback, injected, attempt, hosts, hostIdx, n, host](bool ok, const QByteArray &body, const QString &err) {
                if (!ok) {
                    // 传输失败(域名解析/连接层): 转移到域名列表中的下一个再试。
                    if (hostIdx + 1 < hosts.size()) {
                        Logger::instance().warn(QStringLiteral(
                            "[Dmghg] main gateway host unreachable (%1), failing over: %2")
                            .arg(host, err));
                        (*attempt)(1, hostIdx + 1);
                        return;
                    }
                    callback(false, {}, err);
                    return;
                }
                const QByteArray plain = decryptBody(body);
                if (plain.isEmpty() && !body.trimmed().isEmpty()) {
                    callback(false, {}, QStringLiteral(
                        "动漫接口响应解密失败,请检查 dmghg_key.pem 是否随程序安装"));
                    return;
                }
                const QJsonDocument doc = QJsonDocument::fromJson(plain);
                const int code = doc.isObject() ? doc.object().value(QStringLiteral("code")).toInt() : 0;
                // 请求级日志: 路由/域名/结果码, 用于远端定位动漫菜单等环节的失败点。
                Logger::instance().info(QStringLiteral(
                    "[Dmghg] %1 host=%2 code=%3")
                    .arg(route, host).arg(code));
                if (code != 403501 && code != 30000) {
                    // 只有业务成功/正常应用层响应才标记首选。CDN 后面可能存在单个
                    // 返回 403501 的异常节点; 不能把它提升为后续请求的首选网关。
                    if (gPreferredMainHost != host) {
                        gPreferredMainHost = host;
                        persistGatewayHost(host);
                        // 换了新域名: 稍后向它做一轮域名发现, 学习官方可能下发的迁移域名。
                        gatewayDiscoveryDone_ = false;
                        QTimer::singleShot(3000, this, [this]() { discoverGatewayHosts(); });
                    }
                    callback(true, plain, {});
                    return;
                }
                // 403501/30000: 签名被拒。
                if (injected) {
                    Logger::instance().error(QStringLiteral(
                        "[Dmghg] injected session IV rejected (code=%1 route=%2), recapture it")
                        .arg(code).arg(route));
                    callback(false, {}, QStringLiteral("动漫接口会话已失效，请重新抓取真实 App 会话 IV 并注入"));
                    return;
                }
                // 派生 IV: 先静默重试当前桶 (服务端瞬时抖动/风控常自愈)。
                if (n < kSigRetryAttempts) {
                    Logger::instance().info(QStringLiteral(
                        "[Dmghg] transient signature reject (code=%1 route=%2 attempt=%3/%4), retrying current bucket")
                        .arg(code).arg(route).arg(n).arg(kSigRetryAttempts));
                    QTimer::singleShot(kSigRetryDelayMs, [attempt, n, hostIdx]() { (*attempt)(n + 1, hostIdx); });
                    return;
                }
                // 重试耗尽仍被拒。403501/30000 是应用层鉴权拒绝: CDN 各节点后面是
                // 同一后端, 换主机/IP 重发只是制造无效请求风暴, 反而延长服务端风控
                // 窗口 (2026-09-20 实测: appid 级风控约 3 分钟自愈, 静默等待即恢复)。
                // 因此不做主机故障转移, 直接入队交由后台恢复流程: 时钟漂移则用
                // HTTP Date 校准后重扫, 否则进入风控静默期到期复查。
                Logger::instance().warn(QStringLiteral(
                    "[Dmghg] derived IV rejected after %1 attempts (code=%2 route=%3), handing over to recovery")
                    .arg(kSigRetryAttempts).arg(code).arg(route));
                // 入队待重放: 恢复成功后自动重新加载该路由, 用户无需手动刷新。
                if (pendingSessionReplays_.size() < 8)
                    pendingSessionReplays_.append({route, callback});
                recoverSessionIv();
                callback(false, {}, QStringLiteral("动漫接口会话已失效,正在后台自动恢复会话,请稍后重试"));
            });
    };
    (*attempt)(1, 0);
}

// 后台时间桶恢复。
// 触发条件:callMainGateway 用当前时间派生的 IV 被拒 (403501/30000) —— 通常是本机时钟
// 偏移越过 10000s 桶边界, 或服务端更新了派生逻辑。
// 策略:以当前时间为原点, 对邻近若干 10000s 时间桶 (±N) 逐个用 deriveLiveIv 派生 IV 并探测
// pc/config, 命中即把命中的桶偏移记入全局 gLiveBucketOffset, 后续请求据此偏移派生 IV。
// 异步、串行短超时, 不阻塞已失败的请求回调; 命中/失败均只记日志。
// 限流: ① 并发护栏 (recoveryInProgress_); ② 全局冷却 kRecoveryCooldownMs, 抖动期多个失败请求
// 各自触发完整 ±8 桶扫描会形成「挂了→疯狂扫描→更挂」的恶性循环, 冷却把恢复频率压到每 30s 至多一次;
// ③ 桶间 kRecoveryStepDelayMs 间隔, 降低对服务端的瞬时压力。
namespace {
struct RecoveryState {
    qint64 baseUnix = 0;   // 恢复发起时刻的 unix 秒 (已含 gLiveBucketOffset), 作为桶扫描原点
    bool done = false;
    int resyncs = 0;       // 已用 HTTP Date 校准重扫的次数 (防死循环)
    int rechecks = 0;      // 风控静默期后的复查轮数 (防无限等待)
};
}

void DmghgClient::recoverSessionIv() {
    // 并发护栏:若已有恢复任务在跑,直接返回,避免多个失败请求各自启动扫描。
    bool expected = false;
    if (!recoveryInProgress_.compare_exchange_strong(expected, true)) {
        Logger::instance().info(QStringLiteral("[Dmghg] session recovery already in progress, skipping"));
        return;
    }
    // 全局冷却:距上一次恢复发起不足 kRecoveryCooldownMs 时跳过。
    // (compare_exchange 已保证并发安全; 冷却判定在护栏之内, 静默放行让抖动窗口自行过去。)
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastStart = gLastRecoveryStartMs.load();
    if (lastStart > 0 && nowMs - lastStart < kRecoveryCooldownMs) {
        recoveryInProgress_ = false;  // 释放护栏, 允许冷却结束后下次恢复
        Logger::instance().info(QStringLiteral(
            "[Dmghg] session recovery skipped (cooldown, %1ms since last)")
            .arg(nowMs - lastStart));
        return;
    }
    gLastRecoveryStartMs.store(nowMs);

    auto state = std::make_shared<RecoveryState>();
    state->baseUnix = QDateTime::currentSecsSinceEpoch()
        + gServerTimeSkewSec.load()
        + qint64(gLiveBucketOffset.load()) * 10000;

    // 螺旋偏移序列: 0, +1, -1, +2, -2, ... 对称覆盖邻近时间桶。
    auto nextOffset = [](int step) -> int {
        if (step == 0) return 0;
        return (step % 2 == 1) ? (step + 1) / 2 : -(step / 2);
    };

    auto probeOne = std::make_shared<std::function<void(int)>>();
    *probeOne = [this, state, probeOne, nextOffset](int step) {
        if (state->done) { recoveryInProgress_ = false; return; }
        // ±1 桶 (0,+1,-1): IV 是纯时间派生, 真正的桶错位只可能来自时钟漂移,
        // 而 Date 校准能精确诊断修正; 更深的扫描对着服务端风控毫无意义, 只会
        // 产生无效请求风暴延长窗口 (旧版 ±8 桶是风控放大器, 已实测确认)。
        constexpr int kMaxBucketSteps = 3;
        if (step > kMaxBucketSteps) {
            // 邻近桶均未命中且时钟已校准: 判定为服务端 appid 级风控 (实测约 3 分钟
            // 自愈)。进入全局静默期 (callMainGateway 入口闸门据此拦截新请求), 到期
            // 回到桶 0 单探测复查; 复查通过即恢复并重放排队请求。
            if (state->rechecks >= kRiskControlMaxRechecks) {
                Logger::instance().error(QStringLiteral(
                    "[Dmghg] session recovery failed after %1 risk-control rechecks, giving up")
                    .arg(state->rechecks));
                gRiskControlUntilMs.store(0);
                recoveryInProgress_ = false;
                return;
            }
            state->rechecks += 1;
            const int waitMs = state->rechecks == 1
                ? kRiskControlInitialWaitMs : kRiskControlRecheckDelayMs;
            gRiskControlUntilMs.store(QDateTime::currentMSecsSinceEpoch() + waitMs);
            Logger::instance().warn(QStringLiteral(
                "[Dmghg] nearby buckets rejected with calibrated clock -> server risk control, "
                "silent %1ms then recheck (%2/%3)")
                .arg(waitMs).arg(state->rechecks).arg(kRiskControlMaxRechecks));
            QTimer::singleShot(waitMs, this, [this, probeOne, state]() {
                if (state->done) { recoveryInProgress_ = false; return; }
                // 静默期可能跨越 10000s 桶边界: 重算扫描原点, 保证复查用当前桶。
                state->baseUnix = QDateTime::currentSecsSinceEpoch()
                    + gServerTimeSkewSec.load()
                    + qint64(gLiveBucketOffset.load()) * 10000;
                (*probeOne)(0);
            });
            return;
        }
        const int bucketOffset = nextOffset(step);
        const qint64 probeUnix = state->baseUnix + (qint64(bucketOffset) * 10000);
        const QByteArray iv = deriveLiveIv(probeUnix);

        const qint64 ts = serverNowMs();
        const QString shortTs = QString::number(ts % 10000000);
        const QByteArray plain = QStringLiteral("%1-%2-%3-%4")
            .arg(shortTs, QLatin1String(kSystem), QLatin1String(kProtocolVer), QLatin1String(kPlatform))
            .toUtf8();
        QByteArray inner = plain.toBase64();
        const int pad = 16 - (inner.size() % 16);
        inner.append(QByteArray(pad, char(pad)));
        const QByteArray ct = aesCbcEncryptRawNoPadding(EVP_aes_256_cbc(), kAuthKey, iv, inner);
        const QByteArray auth = QString::fromLatin1((iv + ct).toBase64()).toUtf8();

        const QString probeHost = currentMainHost();
        QNetworkRequest req(QUrl(QStringLiteral("%1/pc/config").arg(probeHost)));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(kSessionProbeTimeoutMs);
        QMap<QByteArray, QByteArray> headers = {
            {"content-type", "application/json"},
            {"accept", "application/json"},
            {"x-version", kXVersion},
            {"appid", kAppId},
            {"system", kSystem},
            {"x-token", ""},
            {"requrl", probeHost.toUtf8()},
            {"ts", QString::number(ts).toUtf8()},
            {"authentication", auth},
            {"user-agent", kUserAgent},
        };
        for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
            req.setRawHeader(it.key(), it.value());
        QNetworkReply *reply = nam_.get(req);
        QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, bucketOffset, probeUnix, state, probeOne, step]() {
            const QByteArray body = reply->readAll();
            reply->deleteLater();
            // 虚拟机时钟漂移是 403501 的常见根因: HTTP Date 头是服务器时间,
            // 用它校准本机偏差后从桶 0 重扫, 无需人工对时。
            const QDateTime serverDate = QDateTime::fromString(
                QString::fromLatin1(reply->rawHeader("Date")), Qt::RFC2822Date);
                if (serverDate.isValid() && state->resyncs < 3) {
                    const qint64 observedSkew = serverDate.toSecsSinceEpoch()
                        - QDateTime::currentSecsSinceEpoch();
                    if (qAbs(observedSkew) > 30
                            && qAbs(observedSkew - gServerTimeSkewSec.load()) > 30) {
                        gServerTimeSkewSec.store(observedSkew);
                        state->resyncs += 1;
                        // 时钟偏差刚被修正: 旧的桶偏移是按未修正时钟推的, 已无意义,
                        // 必须清零。否则 sessionIv() 里 skew + 桶偏移双重叠加,
                        // IV 永远对不上 -> 恢复死循环。
                        gLiveBucketOffset.store(0);
                        state->baseUnix = QDateTime::currentSecsSinceEpoch() + observedSkew;
                        Logger::instance().warn(QStringLiteral(
                            "[Dmghg] server clock skew detected: %1s, rescanning buckets with corrected time")
                            .arg(observedSkew));
                        QTimer::singleShot(100, this, [probeOne]() { (*probeOne)(0); });
                        return;
                    }
                }
            const QByteArray plainBody = DmghgClient::decryptBody(body);
            const QJsonDocument doc = QJsonDocument::fromJson(plainBody);
            const int code = doc.isObject() ? doc.object().value(QStringLiteral("code")).toInt() : 0;
            if (code == 20000) {
                state->done = true;
                gRiskControlUntilMs.store(0);  // 清除静默期, 放行后续主网关请求
                // 命中桶相对「服务器时间」的偏移 (单位=10000s 桶)。sessionIv()
                // 已经叠加了 gServerTimeSkewSec, 这里若再用未修正的本地时间
                // 计算 offset, 偏差会被算两次 -> 修完又坏, 恢复死循环。
                const qint64 live = QDateTime::currentSecsSinceEpoch()
                    + gServerTimeSkewSec.load();
                const int liveOffset = int((probeUnix - live) / 10000);
                gLiveBucketOffset.store(liveOffset);
                Logger::instance().info(QStringLiteral(
                    "[Dmghg] session recovery success: bucket_offset=%1 skew=%2s IV=%3")
                    .arg(liveOffset)
                    .arg(gServerTimeSkewSec.load())
                    .arg(QString::fromLatin1(deriveLiveIv(probeUnix).toHex())));
                recoveryInProgress_ = false;
                // 恢复成功: 重放会话失效期间失败的请求 (列表/详情/播放),
                // 用户看到的报错会在几秒内自动变成加载完成。
                const auto replays = pendingSessionReplays_;
                pendingSessionReplays_.clear();
                int replayDelay = 200;
                for (const auto &pending : replays) {
                    QTimer::singleShot(replayDelay, this, [this, pending]() {
                        Logger::instance().info(QStringLiteral(
                            "[Dmghg] replaying failed route after recovery: %1").arg(pending.route));
                        callMainGateway(pending.route, 0, pending.callback);
                    });
                    replayDelay += 250;
                }
                return;
            }
            Logger::instance().info(QStringLiteral(
                "[Dmghg] session recovery: bucket offset=%1 code=%2, trying next")
                .arg(bucketOffset).arg(code));
            // 桶间加 kRecoveryStepDelayMs 间隔, 降低对服务端的瞬时压力。
            // 复查轮 (风控静默到期) 只探桶 0: 未过即回到静默等待, 不再扫邻桶,
            // 保证风控期间服务端每轮最多只见 1 个探测请求。
            const int nextStep = (state->rechecks > 0 && step >= 0)
                ? kMaxBucketSteps + 1
                : step + 1;
            QTimer::singleShot(kRecoveryStepDelayMs, [probeOne, nextStep]() { (*probeOne)(nextStep); });
        });
    };
    Logger::instance().warn(QStringLiteral("[Dmghg] starting background session IV time-bucket recovery"));
    (*probeOne)(0);
}

// ── 网关域名发现 (概念版 auth bootstrap 的轻量等价) ─────────
// 逆向结论: 概念版 electron_bridge.dll 的 bootstrap.rs 会经引导入口拉取
// 加密的网关域名列表 (bootstrap_host_base64/json 校验链, AES 密钥与会话
// 鉴权同一套), 服务端改引导数据即可全员换域名, 客户端无需发版。
// 加密引导协议细节未完全钉死, 这里做等价实现: 网关的 pc/config 响应里
// 若出现官方域名的 URL (服务端迁移时下发), 自动探测并收录为候选。
namespace {

// 官方业务域名区: 发现扫描只认这些区, 避免把 CDN/图床误收为网关。
bool isOfficialGatewayZone(const QString &host) {
    // 官方域名区以混淆形态内嵌 (源码/二进制均无明文)。
    const QStringList zones = DmghgSecrets::officialGatewayZones();
    for (const QString &zone : zones)
        if (host.endsWith(zone, Qt::CaseInsensitive))
            return true;
    return host.contains(QStringLiteral("xn--"), Qt::CaseInsensitive);  // 中文域名的 punycode 形式
}

}  // namespace

void DmghgClient::discoverGatewayHosts() {
    if (gatewayDiscoveryDone_ || gPreferredMainHost.isEmpty())
        return;
    gatewayDiscoveryDone_ = true;
    const QString base = gPreferredMainHost;
    QNetworkRequest req(QUrl(QStringLiteral("%1/pc/config").arg(base)));
    req.setTransferTimeout(8000);
    const auto headers = buildMainGatewayHeaders(serverNowMs(), base);
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        req.setRawHeader(it.key(), it.value());
    QNetworkReply *reply = nam_.get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, base]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QString text = QString::fromUtf8(decryptBody(reply->readAll()));
        static const QRegularExpression urlPattern(
            QStringLiteral("https?://[A-Za-z0-9.-]+(?::\\d+)?"),
            QRegularExpression::CaseInsensitiveOption);
        const QStringList known = gatewayCandidates();
        QStringList discovered;
        auto match = urlPattern.globalMatch(text);
        while (match.hasNext()) {
            const QUrl url(match.next().captured(0));
            const QString host = url.host().toLower();
            if (host.isEmpty() || !isOfficialGatewayZone(host))
                continue;
            const QString candidate = QStringLiteral("%1://%2").arg(url.scheme().toLower(), host);
            if (known.contains(candidate) || discovered.contains(candidate))
                continue;
            discovered.append(candidate);
        }
        for (const QString &candidate : discovered) {
            Logger::instance().info(QStringLiteral(
                "[Dmghg] host discovery candidate: %1").arg(candidate));
            probeGatewayHost(candidate);
        }
    });
}

void DmghgClient::probeGatewayHost(const QString &host) {
    QNetworkRequest req(QUrl(QStringLiteral("%1/pc/config").arg(host)));
    req.setTransferTimeout(8000);
    const auto headers = buildMainGatewayHeaders(serverNowMs(), host);
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        req.setRawHeader(it.key(), it.value());
    QNetworkReply *reply = nam_.get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, host]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QByteArray plain = decryptBody(reply->readAll());
        const QJsonDocument doc = QJsonDocument::fromJson(plain);
        const int code = doc.isObject() ? doc.object().value(QStringLiteral("code")).toInt() : 0;
        if (code != 20000) {
            Logger::instance().info(QStringLiteral(
                "[Dmghg] discovered host rejected (code=%1): %2").arg(code).arg(host));
            return;
        }
        Logger::instance().info(QStringLiteral(
            "[Dmghg] discovered gateway host accepted: %1").arg(host));
        persistGatewayHost(host);
    });
}

void DmghgClient::queryBootstrapGateway() {
    QUrl url(QStringLiteral("http://139.199.159.222:7862/app/config/host"));
    QNetworkRequest req(url);
    req.setTransferTimeout(4000);
    req.setRawHeader("appid", kAppId);
    req.setRawHeader("system", kSystem);
    req.setRawHeader("x-version", kXVersion);
    req.setRawHeader("user-agent", kUserAgent);
    req.setRawHeader("accept", "application/json");

    QNetworkReply *reply = nam_.get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Logger::instance().info(QStringLiteral("[Dmghg] bootstrap query error: %1").arg(reply->errorString()));
            return;
        }
        syncServerTimeSkew(reply->rawHeader("Date"));
        const QByteArray resp = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(resp);
        if (!doc.isObject()) return;
        const QJsonObject root = doc.object();
        if (root.value(QStringLiteral("code")).toInt() != 20000) return;
        const QString b64Data = root.value(QStringLiteral("data")).toString();
        if (b64Data.isEmpty()) return;
        const QByteArray raw = QByteArray::fromBase64(b64Data.toUtf8());
        if (raw.isEmpty()) return;
        const QByteArray decrypted = aesCbcDecryptRaw(EVP_aes_256_cbc(), kAuthKey, kIvKey2, raw);
        if (decrypted.isEmpty()) return;
        const QByteArray jsonBytes = QByteArray::fromBase64(decrypted);
        const QJsonDocument hostDoc = QJsonDocument::fromJson(jsonBytes);
        if (!hostDoc.isObject()) return;
        const QJsonObject hostObj = hostDoc.object();
        QString mainHost = hostObj.value(QStringLiteral("host")).toString().trimmed();
        if (mainHost.isEmpty() && hostObj.contains(QStringLiteral("hosts"))) {
            const QJsonArray arr = hostObj.value(QStringLiteral("hosts")).toArray();
            if (!arr.isEmpty()) mainHost = arr.first().toString().trimmed();
        }
        if (!mainHost.isEmpty()) {
            if (!mainHost.startsWith(QStringLiteral("http://")) && !mainHost.startsWith(QStringLiteral("https://"))) {
                mainHost = QStringLiteral("http://%1").arg(mainHost);
            }
            Logger::instance().info(QStringLiteral("[Dmghg] bootstrap discovered host: %1").arg(mainHost));
            persistGatewayHost(mainHost);
            const QUrl u(mainHost);
            if (!u.host().isEmpty() && !isIpv4Address(u.host())) {
                resolveGatewayByDoh(u.host());
            }
        }
    });
}

void DmghgClient::resolveGatewayByDoh(const QString &domain) {
    if (domain.isEmpty() || isIpv4Address(domain)) return;
    const QUrl url(QStringLiteral("http://223.5.5.5/resolve?name=%1&type=1").arg(domain));
    QNetworkRequest req(url);
    req.setTransferTimeout(3000);
    req.setRawHeader("accept", "application/json");

    QNetworkReply *reply = nam_.get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [domain, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Logger::instance().info(QStringLiteral("[Dmghg] DoH resolve error for %1: %2").arg(domain, reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) return;
        const QJsonArray answers = doc.object().value(QStringLiteral("Answer")).toArray();
        QStringList ips;
        for (const QJsonValue &ansVal : answers) {
            if (!ansVal.isObject()) continue;
            const QJsonObject ans = ansVal.toObject();
            if (ans.value(QStringLiteral("type")).toInt() == 1) {
                const QString ip = ans.value(QStringLiteral("data")).toString().trimmed();
                if (isIpv4Address(ip) && !ips.contains(ip)) {
                    ips.append(ip);
                }
            }
        }
        if (!ips.isEmpty()) {
            gDynamicCdnIps = ips;
            Logger::instance().info(QStringLiteral("[Dmghg] DoH resolved %1 IPs: %2")
                .arg(domain, ips.join(QStringLiteral(", "))));
        }
    });
}

void DmghgClient::callSearchGateway(const QString &keyword, int page, int limit, qint64 nowSec,
        const std::function<void(bool, const QByteArray &, const QString &)> &callback) {
    const QString tStr = QString::number(nowSec);
    const QByteArray sign = md5Hex(QStringLiteral("%1/pc/video/search%2")
        .arg(QString::fromLatin1(kSearchSecretBa), tStr).toUtf8());
    auto makeQuery = [keyword, page, limit](QUrl url, bool withSign,
                                             const QByteArray &signValue, const QString &timeValue) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("key"), keyword);
        q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
        q.addQueryItem(QStringLiteral("page"), QString::number(page));
        if (withSign) {
            q.addQueryItem(QStringLiteral("sign"), QString::fromLatin1(signValue));
            q.addQueryItem(QStringLiteral("t"), timeValue);
        }
        url.setQuery(q);
        return url;
    };

    // 文档定义的独立搜索网关当前可能返回 503；失败时必须回退主网关。
    // 主网关使用会话 Authentication，仅携带 key/limit/page，不带独立网关 sign/t。
    auto requestGateway = std::make_shared<std::function<void(bool)>>();
    *requestGateway = [this, callback, makeQuery, requestGateway, sign, tStr](bool fallback) {
        const bool useFallback = fallback;
        const QString base = useFallback ? currentMainHost()
                                         : QString::fromLatin1(kSearchHost);
        QUrl url = makeQuery(QUrl(QStringLiteral("%1/pc/video/search").arg(base)),
                             !useFallback, sign, tStr);
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(8000);

        QMap<QByteArray, QByteArray> headers;
        if (useFallback) {
            headers = buildMainGatewayHeaders(serverNowMs(), base);
        } else {
            headers.insert("accept", "application/json");
            headers.insert("user-agent", kUserAgent);
            headers.insert("connection", "close");
        }
        for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
            req.setRawHeader(it.key(), it.value());

        QNetworkReply *reply = nam_.get(req);
        QObject::connect(reply, &QNetworkReply::finished, reply,
            [this, callback, requestGateway, reply, useFallback]( ) {
                const auto networkError = reply->error();
                const QByteArray body = reply->readAll();
                const QString errorText = reply->errorString();
                reply->deleteLater();

                if (networkError == QNetworkReply::NoError && !body.trimmed().isEmpty()) {
                    const QByteArray plain = decryptBody(body);
                    const QJsonDocument doc = QJsonDocument::fromJson(plain);
                    const int responseCode = doc.isObject()
                        ? doc.object().value(QStringLiteral("code")).toInt() : -1;
                    const bool validSearchResponse = doc.isObject()
                        && doc.object().contains(QStringLiteral("code"))
                        && (responseCode == 0 || responseCode == 20000);
                    if (validSearchResponse) {
                        callback(true, plain, {});
                        return;
                    }
                }

                if (!useFallback) {
                    Logger::instance().warn(QStringLiteral("[Dmghg] search gateway unavailable, falling back to main gateway"));
                    (*requestGateway)(true);
                    return;
                }

                callback(false, {}, errorText.isEmpty()
                    ? QStringLiteral("动漫搜索网关返回无效响应") : errorText);
            });
    };
    (*requestGateway)(false);
}

void DmghgClient::callMainPost(const QString &route, const QByteArray &payload,
        const std::function<void(bool, const QByteArray &, const QString &)> &callback) {
    const QStringList hosts = gatewayCandidates();
    auto tryHost = std::make_shared<std::function<void(int)>>();
    *tryHost = [this, route, payload, callback, tryHost, hosts](int hostIdx) {
        QNetworkRequest request(QUrl(QStringLiteral("%1/%2").arg(hosts.at(hostIdx), route)));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(12000);
        const auto headers = buildMainGatewayHeaders(serverNowMs(), hosts.at(hostIdx));
        for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
            request.setRawHeader(it.key(), it.value());
        QNetworkReply *reply = nam_.post(request, payload);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback, tryHost, hosts, hostIdx]() {
            const auto error = reply->error();
            const QByteArray body = reply->readAll();
            const QString errorText = reply->errorString();
            reply->deleteLater();
            if (error != QNetworkReply::NoError && body.isEmpty()) {
                // 传输失败: 换下一个网关域名再试。
                if (hostIdx + 1 < hosts.size()) { (*tryHost)(hostIdx + 1); return; }
                callback(false, {}, errorText);
                return;
            }
            if (error == QNetworkReply::NoError && gPreferredMainHost != hosts.at(hostIdx)) {
                gPreferredMainHost = hosts.at(hostIdx);
                persistGatewayHost(gPreferredMainHost);
            }
            callback(true, DmghgClient::decryptBody(body), {});
        });
    };
    (*tryHost)(0);
}

QByteArray DmghgClient::decryptBody(const QByteArray &body) {
    QByteArray txt = body.trimmed();
    if (txt.isEmpty()) return {};
    if (txt.startsWith('{')) return txt; // 明文错误/拦截态
    if (!txt.contains('.')) return txt;
    const int dot = txt.indexOf('.');
    const QByteArray p1 = QByteArray::fromBase64(txt.left(dot));
    const QByteArray p2 = QByteArray::fromBase64(txt.mid(dot + 1));
    if (p1.size() != 256) return {};
    const QByteArray aesKey = rsaPkcs1Decrypt(p1);
    if (aesKey.size() != 16) return {};
    QByteArray iv = aesKey;
    std::reverse(iv.begin(), iv.end());
    return aesCbcDecryptRaw(EVP_aes_128_cbc(), aesKey, iv, p2);
}

QByteArray DmghgClient::aes128CbcDecrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &base64Data) {
    return aesCbcDecryptBase64(EVP_aes_128_cbc(), key, iv, base64Data);
}

QByteArray DmghgClient::aes128CbcEncrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &plain) {
    return aesCbcEncryptBase64(EVP_aes_128_cbc(), key, iv, plain);
}

void DmghgClient::search(const QString &keyword, int page, int limit,
        const std::function<void(bool, const QVariantList &, const QString &)> &callback) {
    callSearchGateway(keyword, page, limit, QDateTime::currentSecsSinceEpoch(),
        [callback](bool ok, const QByteArray &body, const QString &err) {
            if (!ok) { callback(false, {}, err); return; }
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
            if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
                callback(false, {}, QStringLiteral("search JSON parse failed"));
                return;
            }
            const QJsonObject root = doc.object();
            const int code = root.value(QStringLiteral("code")).toInt();
            if (code != 0 && code != 20000) {
                callback(false, {}, QStringLiteral("search code=%1").arg(root.value(QStringLiteral("code")).toInt()));
                return;
            }
            const QJsonObject data = root.value(QStringLiteral("data")).toObject();
            QJsonArray items = data.value(QStringLiteral("items")).toArray();
            if (items.isEmpty())
                items = data.value(QStringLiteral("list")).toArray();
            QVariantList out;
            for (const QJsonValue &v : items) {
                const QJsonObject o = v.toObject();
                QVariantMap m;
                int vodId = o.value(QStringLiteral("id")).toInt();
                if (vodId <= 0) vodId = o.value(QStringLiteral("vod_id")).toInt();
                if (vodId <= 0) vodId = o.value(QStringLiteral("video_id")).toInt();
                QString name = strField(o, QStringLiteral("name"));
                if (name.isEmpty()) name = strField(o, QStringLiteral("title"));
                QString pic = strField(o, QStringLiteral("pic"));
                if (pic.isEmpty()) pic = strField(o, QStringLiteral("cover"));
                QString remarks = strField(o, QStringLiteral("continu"));
                if (remarks.isEmpty()) remarks = strField(o, QStringLiteral("remarks"));
                m.insert(QStringLiteral("vodId"), vodId);
                m.insert(QStringLiteral("vodName"), MediaUtils::sanitizeUserText(name));
                m.insert(QStringLiteral("vodPic"), pic);
                m.insert(QStringLiteral("vodRemarks"), MediaUtils::sanitizeUserText(remarks));
                m.insert(QStringLiteral("typeName"), MediaUtils::sanitizeUserText(strField(o, QStringLiteral("type"))));
                m.insert(QStringLiteral("vodYear"), strField(o, QStringLiteral("year")));
                m.insert(QStringLiteral("vodArea"), MediaUtils::sanitizeUserText(strField(o, QStringLiteral("area"))));
                out.append(m);
            }
            callback(true, out, {});
        });
}

void DmghgClient::listVideos(int channel, int page, int limit, const QString &sort, const QString &type,
        const std::function<void(bool, const QVariantList &, int total, const QString &)> &callback) {
    QString route = QStringLiteral("pc/video/list?channel=%1&limit=%2&page=%3&sort=%4")
        .arg(channel).arg(limit).arg(page).arg(sort);
    if (!type.trimmed().isEmpty())
        route += QStringLiteral("&type=") + QString::fromLatin1(QUrl::toPercentEncoding(type.trimmed()));
    callMainGateway(route, QDateTime::currentMSecsSinceEpoch(),
        [callback](bool ok, const QByteArray &body, const QString &err) {
            if (!ok) { callback(false, {}, 0, err); return; }
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (!doc.isObject()) { callback(false, {}, 0, QStringLiteral("list JSON parse failed")); return; }
            const QJsonObject root = doc.object();
            const int code = root.value(QStringLiteral("code")).toInt();
            if (code != 0 && code != 20000) {
                callback(false, {}, 0, QStringLiteral("list code=%1").arg(code));
                return;
            }
            const QJsonObject data = root.value(QStringLiteral("data")).toObject();
            const QJsonValue totalValue = data.value(QStringLiteral("total"));
            const int total = totalValue.isString()
                ? totalValue.toString().toInt()
                : totalValue.toInt();
            QJsonArray items = data.value(QStringLiteral("items")).toArray();
            if (items.isEmpty())
                items = data.value(QStringLiteral("list")).toArray();
            QVariantList out;
            for (const QJsonValue &v : items) {
                const QJsonObject o = v.toObject();
                QVariantMap m;
                m.insert(QStringLiteral("vodId"), o.value(QStringLiteral("id")).toInt());
                QString name = strField(o, QStringLiteral("name"));
                if (name.isEmpty()) name = strField(o, QStringLiteral("title"));
                QString pic = strField(o, QStringLiteral("pic"));
                if (pic.isEmpty()) pic = strField(o, QStringLiteral("cover"));
                QString remarks = strField(o, QStringLiteral("continu"));
                if (remarks.isEmpty()) remarks = strField(o, QStringLiteral("remarks"));
                m.insert(QStringLiteral("vodName"), MediaUtils::sanitizeUserText(name));
                m.insert(QStringLiteral("vodPic"), pic);
                m.insert(QStringLiteral("vodRemarks"), MediaUtils::sanitizeUserText(remarks));
                m.insert(QStringLiteral("typeName"), MediaUtils::sanitizeUserText(strField(o, QStringLiteral("type"))));
                m.insert(QStringLiteral("vodYear"), strField(o, QStringLiteral("year")));
                m.insert(QStringLiteral("vodArea"), MediaUtils::sanitizeUserText(strField(o, QStringLiteral("area"))));
                out.append(m);
            }
            callback(true, out, total, {});
        });
}

// 旧协议没有下拉接口：频道是写死的整数 id、类型来自 pc/channel 的标签，界面沿用常量表。
void DmghgClient::loadChannelOptions(const FilterCb &callback) { callback(true, {}, {}); }
void DmghgClient::loadTypeOptions(const FilterCb &callback) { callback(true, {}, {}); }
void DmghgClient::loadWeeklySchedule(
        const std::function<void(bool, const QVariantMap &, const QString &)> &callback) {
    if (callback)
        callback(false, {}, QStringLiteral("旧协议不支持每周更新排播"));
}

// 旧协议只有整数编号：非纯数字或超出 int 范围的（新版 18 位 id）都认不出来。
int DmghgClient::handleForKey(const QString &key) {
    bool fits = false;
    const int asInt = key.trimmed().toInt(&fits);
    return (fits && asInt > 0) ? asInt : 0;
}

void DmghgClient::videoDetail(int vid,
        const std::function<void(bool, const QVariantMap &, const QString &)> &callback) {
    const QString route = QStringLiteral("pc/video/detail?id=%1").arg(vid);
    callMainGateway(route, QDateTime::currentMSecsSinceEpoch(),
        [callback](bool ok, const QByteArray &body, const QString &err) {
            if (!ok) { callback(false, {}, err); return; }
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (!doc.isObject()) { callback(false, {}, QStringLiteral("detail JSON parse failed")); return; }
            const QJsonObject root = doc.object();
            if (root.value(QStringLiteral("code")).toInt() != 20000) {
                callback(false, {}, QStringLiteral("detail code=%1").arg(root.value(QStringLiteral("code")).toInt()));
                return;
            }
            const QJsonObject d = root.value(QStringLiteral("data")).toObject();
            QVariantMap m;
            m.insert(QStringLiteral("vodId"), d.value(QStringLiteral("id")).toInt());
            m.insert(QStringLiteral("vodName"), MediaUtils::sanitizeUserText(strField(d, QStringLiteral("name"))));
            m.insert(QStringLiteral("vodPic"), strField(d, QStringLiteral("pic")));
            m.insert(QStringLiteral("vodRemarks"), MediaUtils::sanitizeUserText(strField(d, QStringLiteral("continu"))));
            m.insert(QStringLiteral("vodYear"), strField(d, QStringLiteral("year")));
            m.insert(QStringLiteral("vodArea"), strField(d, QStringLiteral("area")));
            m.insert(QStringLiteral("typeName"), strField(d, QStringLiteral("type")));
            m.insert(QStringLiteral("vodActor"), MediaUtils::sanitizeUserText(strField(d, QStringLiteral("actor"))));
            m.insert(QStringLiteral("vodDirector"), MediaUtils::sanitizeUserText(strField(d, QStringLiteral("director"))));
            m.insert(QStringLiteral("vodContent"), MediaUtils::sanitizeUserText(strField(d, QStringLiteral("content"))));
            m.insert(QStringLiteral("vodScore"), strField(d, QStringLiteral("score")));

            // parts:[{play, part:[集名,...]}] -> 播放源数组
            QVariantList sources;
            const QJsonArray parts = d.value(QStringLiteral("parts")).toArray();
            for (const QJsonValue &pv : parts) {
                const QJsonObject po = pv.toObject();
                QVariantMap src;
                src.insert(QStringLiteral("name"), strField(po, QStringLiteral("play")));
                if (src.value(QStringLiteral("name")).toString().isEmpty())
                    src.insert(QStringLiteral("name"), QStringLiteral("默认线路"));
                QVariantList eps;
                const QJsonArray partArr = po.value(QStringLiteral("part")).toArray();
                for (const QJsonValue &ev : partArr) {
                    if (ev.isString()) eps.append(ev.toString());
                    else if (ev.isDouble()) eps.append(QString::number(ev.toInt()));
                }
                src.insert(QStringLiteral("episodes"), eps);
                sources.append(src);
            }
            m.insert(QStringLiteral("playSources"), sources);
            callback(true, m, {});
        });
}

void DmghgClient::playVideo(int vid, const QString &part, const QString &playSource,
        const std::function<void(bool, const QString &, const QString &)> &callback) {
    const QString route = QStringLiteral("pc/video/play?id=%1&part=%2&play=%3")
        .arg(vid)
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(part)))
        .arg(playSource.isEmpty() ? QStringLiteral("cn") : playSource);
    callMainGateway(route, QDateTime::currentMSecsSinceEpoch(),
        [callback](bool ok, const QByteArray &body, const QString &err) {
            if (!ok) { callback(false, {}, err); return; }
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (!doc.isObject()) { callback(false, {}, QStringLiteral("play JSON parse failed")); return; }
            const QJsonObject root = doc.object();
            if (root.value(QStringLiteral("code")).toInt() != 20000) {
                callback(false, {}, QStringLiteral("play code=%1").arg(root.value(QStringLiteral("code")).toInt()));
                return;
            }
            const QJsonArray data = root.value(QStringLiteral("data")).toArray();
            if (data.isEmpty()) { callback(false, {}, QStringLiteral("play data empty")); return; }
            const QString source = data.at(0).toObject().value(QStringLiteral("url")).toString();
            callback(true, source, {});
        });
}

void DmghgClient::parseSource(const QString &source,
        const std::function<void(bool, const QVariantList &, const QString &)> &callback) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int baseEndpointIndex = gPreferredJxEndpoint.load();
    const int endpointCount = static_cast<int>(kJxEndpoints.size());
    auto tryEndpoint = std::make_shared<std::function<void(int)>>();
    *tryEndpoint = [this, source, callback, tryEndpoint, baseEndpointIndex, endpointCount, now](int endpointIdx) {
        const QString endpoint = kJxEndpoints.at(((baseEndpointIndex + endpointIdx) % endpointCount
            + endpointCount) % endpointCount);
        const auto [url, headers] = getApiUrl(source, now, endpoint);
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(8000);
        QMap<QByteArray, QByteArray> hdrs = headers;
        hdrs.insert("User-Agent", kUserAgent);
        // 解析网关偶尔会在首次连接时主动关闭连接，增加短间隔重试避免用户必须手动刷新。
        fetchWithRetry(nam_, req, hdrs, 5, 900, [callback, tryEndpoint, baseEndpointIndex, endpointIdx, endpointCount, endpoint](bool ok, const QByteArray &body, const QString &err) {
            if (!ok) {
                // 传输失败: 域名不可达(DNS/连接层), 转移到下一个解析域名。
                if (endpointIdx + 1 < endpointCount) {
                    Logger::instance().warn(QStringLiteral(
                        "[Dmghg] jx endpoint unreachable (%1), failing over").arg(endpoint));
                    (*tryEndpoint)(endpointIdx + 1);
                    return;
                }
                callback(false, {}, err);
                return;
            }
            gPreferredJxEndpoint.store((baseEndpointIndex + endpointIdx) % endpointCount);
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (!doc.isObject()) { callback(false, {}, QStringLiteral("parse JSON failed")); return; }
            const QJsonObject root = doc.object();
            if (root.value(QStringLiteral("code")).toInt() != 0) {
                callback(false, {}, QStringLiteral("parse code=%1").arg(root.value(QStringLiteral("code")).toInt()));
                return;
            }
            const QJsonArray playAddrs = root.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("playAddr")).toArray();
            QVariantList out;
            for (int i = 0; i < playAddrs.size(); ++i) {
                const QVariantMap item = processPlayAddr(playAddrs.at(i).toObject(), i);
                if (!item.isEmpty()) out.append(item);
            }
            if (out.isEmpty()) { callback(false, {}, QStringLiteral("parse no playable url")); return; }
            callback(true, out, {});
        });
    };
    (*tryEndpoint)(0);
}

void DmghgClient::comments(int vid, int page, int limit,
        const std::function<void(bool, const QVariantList &, int, const QString &)> &callback) {
    callMainGateway(QStringLiteral("pc/vod_comment/getlist?vid=%1&page=%2&limit=%3").arg(vid).arg(page).arg(limit),
        QDateTime::currentMSecsSinceEpoch(), [callback](bool ok, const QByteArray &body, const QString &err) {
        if (!ok) { callback(false, {}, 0, err); return; }
        const auto doc = QJsonDocument::fromJson(body); if (!doc.isObject()) { callback(false, {}, 0, QStringLiteral("comments JSON parse failed")); return; }
        const auto root = doc.object(); if (root.value(QStringLiteral("code")).toInt() != 20000) { callback(false, {}, 0, QStringLiteral("comments code=%1").arg(root.value(QStringLiteral("code")).toInt())); return; }
        const auto data = root.value(QStringLiteral("data")).toObject(); QVariantList out;
        for (const auto &v : data.value(QStringLiteral("items")).toArray()) { const auto o = v.toObject(); QVariantMap m;
            m.insert(QStringLiteral("id"), o.value(QStringLiteral("id")).toInt()); m.insert(QStringLiteral("uname"), MediaUtils::sanitizeUserText(strField(o, QStringLiteral("uname"))));
            m.insert(QStringLiteral("comments"), MediaUtils::sanitizeUserText(strField(o, QStringLiteral("comments")))); m.insert(QStringLiteral("createdAt"), strField(o, QStringLiteral("created_at")));
            m.insert(QStringLiteral("hits"), o.value(QStringLiteral("hits")).toInt()); m.insert(QStringLiteral("isTop"), o.value(QStringLiteral("istop")).toInt() != 0); out.append(m); }
        callback(true, out, data.value(QStringLiteral("total")).toInt(), {});
    });
}

void DmghgClient::createComment(const QString &comment, const QString &uuid, const QString &dots,
        const std::function<void(bool, const QString &)> &callback) {
    QJsonObject payload; payload.insert(QStringLiteral("comment"), comment); payload.insert(QStringLiteral("uuid"), uuid); payload.insert(QStringLiteral("dots"), dots);
    callMainPost(QStringLiteral("pc/vod_comment/create"), QJsonDocument(payload).toJson(QJsonDocument::Compact), [callback](bool ok, const QByteArray &body, const QString &err) { if (!ok) { callback(false, err); return; } const int code = QJsonDocument::fromJson(body).object().value(QStringLiteral("code")).toInt(); callback(code == 20000, code == 20000 ? QString() : QStringLiteral("comment code=%1").arg(code)); });
}

void DmghgClient::danmaku(int vid, const QString &part, const QString &play, int startMs, int endMs,
        const std::function<void(bool, const QVariantList &, int, const QString &)> &callback) {
    // 契约(实抓): GET /pc/danmu?vid=&play=&part=&start_time_point=&end_time_point= (毫秒)。
    // 无需登录态,走标准 Authentication 头(IV 时间派生)。
    const QString route = QStringLiteral("pc/danmu?vid=%1&play=%2&part=%3&start_time_point=%4&end_time_point=%5")
        .arg(vid)
        .arg(play.isEmpty() ? QStringLiteral("cn") : play)
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(part)))
        .arg(startMs)
        .arg(endMs);
    callMainGateway(route, QDateTime::currentMSecsSinceEpoch(),
        [callback](bool ok, const QByteArray &body, const QString &err) {
            if (!ok) { callback(false, {}, 0, err); return; }
            const auto doc = QJsonDocument::fromJson(body);
            if (!doc.isObject()) { callback(false, {}, 0, QStringLiteral("danmaku JSON parse failed")); return; }
            const auto root = doc.object();
            if (root.value(QStringLiteral("code")).toInt() != 20000) {
                callback(false, {}, 0, QStringLiteral("danmaku code=%1").arg(root.value(QStringLiteral("code")).toInt()));
                return;
            }
            const auto data = root.value(QStringLiteral("data")).toObject();
            const auto items = data.value(QStringLiteral("items")).toArray();
            QVariantList out;
            out.reserve(items.size());
            for (const auto &v : items) {
                const auto o = v.toObject();
                const qint64 id = o.value(QStringLiteral("id")).toVariant().toLongLong();
                const qint64 timePoint = o.value(QStringLiteral("time_point")).toVariant().toLongLong();
                // content 是 JSON 字符串: {color(ARGB int), content(text), size?:S|M|L, type?:0=滚动}
                const QJsonDocument inner = QJsonDocument::fromJson(
                    o.value(QStringLiteral("content")).toString().toUtf8());
                const QJsonObject c = inner.object();
                QVariantMap m;
                m.insert(QStringLiteral("id"), id);
                m.insert(QStringLiteral("timePoint"), timePoint);
                m.insert(QStringLiteral("text"), MediaUtils::sanitizeUserText(c.value(QStringLiteral("content")).toString()));
                m.insert(QStringLiteral("color"), c.value(QStringLiteral("color")).toVariant().toLongLong());
                m.insert(QStringLiteral("mode"), c.value(QStringLiteral("type")).toInt(0)); // 0=滚动
                m.insert(QStringLiteral("size"), c.value(QStringLiteral("size")).toString(QStringLiteral("M")));
                out.append(m);
            }
            callback(true, out, data.value(QStringLiteral("total")).toInt(), {});
        });
}
