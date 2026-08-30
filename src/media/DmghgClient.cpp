#include "DmghgClient.h"
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

// ── 主网关常量 (移植自 dmghg_api.py) ──────────────────
constexpr const char *kHost = "http://bljhm.dokiapp.tech";
constexpr const char *kAppId = "4150439554430627";
constexpr const char *kXVersion = "2024-09-24";
constexpr const char *kSystem = "3";
constexpr const char *kProtocolVer = "1.0.0";
constexpr const char *kPlatform = "win32";
constexpr const char *kUserAgent = "okhttp/4.12.0";

// AES-256-CBC 鉴权密钥 (.rdata @ 0x1806b3763, 逆 electron_bridge.dll 确认)。
const QByteArray kAuthKey = QByteArrayLiteral("ziISjqkXPsGUMRNGyWigxDGtJbfTdcGv");
// 会话 IV 派生的 XOR 掩码 (16B, .rdata 紧跟 kAuthKey @ 0x1806b3783)。
const QByteArray kIvKey2 = QByteArrayLiteral("WonrnVkxeIxDcFbv");
// 桌面版协议版本 (IV 明文前缀, 由反解 base64 还原确认)。
constexpr const char *kIvProtocolVer = "1.4.4";

// 会话 IV 派生算法 (100% 还原自 electron_bridge.dll, 经抓包/历史/实时三重验证):
//     N         = unix_seconds // 10000
//     plaintext = "1.4.4-" + str(N)
//     b64       = base64(plaintext)
//     iv_input  = kIvKey2 XOR b64[:16]              (16B)
//     IV        = AES-256-ECB-ENCRYPT(kAuthKey, iv_input)
// IV 是 unix//10000 的纯确定性函数: 同一时刻任意机器派生出同一 IV, 无需 epoch/counter
// 探测或服务端下发。每 10000s(≈2.78h) 变化一次; 因 base64 字符非单调, iv_input 末两字节
// (旧称 epoch/counter) 看似「漂移/倒退」, 实则完全确定 —— 历史 08-23→0x33、08-24→0x37、
// 08-25→0x3b、08-26→0x01 均为该公式的自然值, 非服务端轮换。两台机器同一时刻必同 IV,
// 故原先的 (epoch,counter) 探测 + 螺旋全扫自愈机制已无必要, 直接派生即可。
// 403501/30000 在此模型下只可能源自本机时钟偏移过大或服务端更新了派生逻辑;
// 后者可经 DMGHG_SESSION_IV / dmghg/sessionIv 注入真实抓取的 IV 兜底 (见 injectedSessionIv)。
constexpr int kMainGatewayTimeoutMs = 8000;
constexpr int kSessionProbeTimeoutMs = 1000;
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
// 上一次恢复扫描发起的 unix 毫秒; 配合 kRecoveryCooldownMs 限流。
std::atomic<qint64> gLastRecoveryStartMs{0};

// 搜索独立网关与 sign 密钥。
constexpr const char *kSearchSecret = "6yOB7NONSOyeU7XAZkHdcJKpWdYmeBNW";
constexpr const char *kSearchHost = "http://tx.xn--vhqr42drhf5k7b.com";

// ── jx 二次解析常量 (移植自 jx_parse.py) ──────────────
constexpr const char *kJxApiEndpoint = "http://new.jx.dokiapp.tech/?md5=";
const QByteArray kJxAesKey = QByteArrayLiteral("UY9kxQEtk8Dn08Kr");
const QByteArray kJxAesIv = QByteArrayLiteral("J5jQnzGVRfCe4CUk");
constexpr const char *kJxAuthCfgKey = "EV330AVbUcpZz5csLp6k8g4XYAqlbWG";
constexpr int kJxAuthExpire = 600;
constexpr const char *kJxHdrAuthKey = "T8qW2mN6rY4pLc9V";
constexpr const char *kJxHdrProofKey = "C6rX9mQ2tV7pLs4N";
constexpr const char *kJxHdrProbeKey = "522828731F1A016B";
constexpr const char *kJxHdrCheckKey = "522828731F1A016B";

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

QMap<QByteArray, QByteArray> buildMainGatewayHeaders(qint64 tsMs) {
    const QString tsStr = QString::number(tsMs);
    return {
        {"content-type", "application/json"},
        {"accept", "application/json"},
        {"x-version", kXVersion},
        {"appid", kAppId},
        {"system", kSystem},
        {"x-token", ""},
        {"requrl", kHost},
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

std::pair<QString, QMap<QByteArray, QByteArray>> getApiUrl(const QString &source, qint64 now) {
    QString md5Source = source;
    if (md5Source.startsWith(QStringLiteral("new-"))) md5Source = md5Source.mid(4);
    const QString apiSource = encryptMd5Id(md5Source);
    const QString authParam = generateAuthParams(QStringLiteral("/"), kJxAuthExpire, kJxAuthCfgKey, now);
    auto headers = buildApiClientHeaders(QStringLiteral("/"), apiSource, authParam, now);
    const QString url = QStringLiteral("%1%2&%3").arg(kJxApiEndpoint, apiSource, authParam);
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
    url.replace(QStringLiteral("play.ddmm.hzhcbkj.cn"), QStringLiteral("m3.dokiapp.tech"));
    url.replace(QStringLiteral("new.ddmm.hzhcbkj.cn"), QStringLiteral("m3.dokiapp.tech"));
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
            const QNetworkReply::NetworkError err = reply->error();
            const QByteArray body = reply->readAll();
            reply->deleteLater();
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
// 与公式一致). 实测同一正确时间桶的同一 IV, 服务器会间歇性返回 403501 后又放行 —— 多为反复
// 探测触发的瞬时风控或服务端抖动, 而非算法/时钟问题. 因此先对当前桶静默重试若干次再判定:
//   注入 IV 命中 403501/30000 -> 注入值本身失效, 直接引导重新抓取, 不重试;
//   派生 IV 重试耗尽仍 403501/30000 -> 才认为本机时钟偏移或服务端换桶, 启动时间桶恢复.
// 每次重试用新 ts 重新派生 IV: 若期间越过 10000s 桶边界, IV 自然更新, 兼顾时钟漂移情形.
constexpr int kSigRetryAttempts = 3;   // 含首次, 共 3 次 (2 次静默重试)
constexpr int kSigRetryDelayMs = 800;

void DmghgClient::callMainGateway(const QString &route, qint64 nowMs,
        const std::function<void(bool, const QByteArray &, const QString &)> &callback) {
    Q_UNUSED(nowMs);  // IV 现按当前实时时间派生, 调用方传入的 nowMs 仅保留签名兼容。
    const bool injected = usingInjectedIv();
    auto attempt = std::make_shared<std::function<void(int)>>();
    *attempt = [this, route, callback, injected, attempt](int n) {
        QUrl url(QStringLiteral("%1/%2").arg(kHost, route));
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(kMainGatewayTimeoutMs);
        fetchWithRetry(nam_, req, buildMainGatewayHeaders(QDateTime::currentMSecsSinceEpoch()),
            2, 500,
            [this, route, callback, injected, attempt, n](bool ok, const QByteArray &body, const QString &err) {
                if (!ok) { callback(false, {}, err); return; }
                const QByteArray plain = decryptBody(body);
                if (plain.isEmpty() && !body.trimmed().isEmpty()) {
                    callback(false, {}, QStringLiteral(
                        "动漫接口响应解密失败,请检查 dmghg_key.pem 是否随程序安装"));
                    return;
                }
                const QJsonDocument doc = QJsonDocument::fromJson(plain);
                const int code = doc.isObject() ? doc.object().value(QStringLiteral("code")).toInt() : 0;
                if (code != 403501 && code != 30000) {
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
                    QTimer::singleShot(kSigRetryDelayMs, [attempt, n]() { (*attempt)(n + 1); });
                    return;
                }
                // 重试耗尽仍被拒: 判定为非瞬时, 启动后台时间桶恢复扫描。
                Logger::instance().warn(QStringLiteral(
                    "[Dmghg] derived IV rejected after %1 attempts (code=%2 route=%3), running time-bucket recovery")
                    .arg(kSigRetryAttempts).arg(code).arg(route));
                recoverSessionIv();
                callback(false, {}, QStringLiteral("动漫接口会话已失效,正在后台自动恢复会话,请稍后重试"));
            });
    };
    (*attempt)(1);
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
        + qint64(gLiveBucketOffset.load()) * 10000;

    // 螺旋偏移序列: 0, +1, -1, +2, -2, ... 对称覆盖邻近时间桶。
    auto nextOffset = [](int step) -> int {
        if (step == 0) return 0;
        return (step % 2 == 1) ? (step + 1) / 2 : -(step / 2);
    };

    auto probeOne = std::make_shared<std::function<void(int)>>();
    *probeOne = [this, state, probeOne, nextOffset](int step) {
        if (state->done) { recoveryInProgress_ = false; return; }
        constexpr int kMaxBucketSteps = 16;  // ±8 桶 ≈ ±8×10000s, 远超任何时钟漂移
        if (step > kMaxBucketSteps) {
            Logger::instance().error(QStringLiteral(
                "[Dmghg] session recovery failed: no live IV in nearby time buckets"));
            recoveryInProgress_ = false;
            return;
        }
        const int bucketOffset = nextOffset(step);
        const qint64 probeUnix = state->baseUnix + (qint64(bucketOffset) * 10000);
        const QByteArray iv = deriveLiveIv(probeUnix);

        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        const QString shortTs = QString::number(ts % 10000000);
        const QByteArray plain = QStringLiteral("%1-%2-%3-%4")
            .arg(shortTs, QLatin1String(kSystem), QLatin1String(kProtocolVer), QLatin1String(kPlatform))
            .toUtf8();
        QByteArray inner = plain.toBase64();
        const int pad = 16 - (inner.size() % 16);
        inner.append(QByteArray(pad, char(pad)));
        const QByteArray ct = aesCbcEncryptRawNoPadding(EVP_aes_256_cbc(), kAuthKey, iv, inner);
        const QByteArray auth = QString::fromLatin1((iv + ct).toBase64()).toUtf8();

        QNetworkRequest req(QUrl(QStringLiteral("%1/pc/config").arg(kHost)));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(kSessionProbeTimeoutMs);
        QMap<QByteArray, QByteArray> headers = {
            {"content-type", "application/json"},
            {"accept", "application/json"},
            {"x-version", kXVersion},
            {"appid", kAppId},
            {"system", kSystem},
            {"x-token", ""},
            {"requrl", kHost},
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
            const QByteArray plainBody = DmghgClient::decryptBody(body);
            const QJsonDocument doc = QJsonDocument::fromJson(plainBody);
            const int code = doc.isObject() ? doc.object().value(QStringLiteral("code")).toInt() : 0;
            if (code == 20000) {
                state->done = true;
                // 命中桶相对「当前实时时间」的偏移 (单位=10000s 桶), 写入后 sessionIv() 据此派生。
                const qint64 live = QDateTime::currentSecsSinceEpoch();
                const int liveOffset = int((probeUnix - live) / 10000);
                gLiveBucketOffset.store(liveOffset);
                Logger::instance().info(QStringLiteral(
                    "[Dmghg] session recovery success: bucket_offset=%1 IV=%2")
                    .arg(liveOffset)
                    .arg(QString::fromLatin1(deriveLiveIv(probeUnix).toHex())));
                recoveryInProgress_ = false;
                return;
            }
            Logger::instance().info(QStringLiteral(
                "[Dmghg] session recovery: bucket offset=%1 code=%2, trying next")
                .arg(bucketOffset).arg(code));
            // 桶间加 kRecoveryStepDelayMs 间隔, 降低对服务端的瞬时压力。
            QTimer::singleShot(kRecoveryStepDelayMs, [probeOne, step]() { (*probeOne)(step + 1); });
        });
    };
    Logger::instance().warn(QStringLiteral("[Dmghg] starting background session IV time-bucket recovery"));
    (*probeOne)(0);
}

void DmghgClient::callSearchGateway(const QString &keyword, int page, int limit, qint64 nowSec,
        const std::function<void(bool, const QByteArray &, const QString &)> &callback) {
    const QString tStr = QString::number(nowSec);
    const QByteArray sign = md5Hex(QStringLiteral("%1/pc/video/search%2")
        .arg(kSearchSecret, tStr).toUtf8());
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
        const QString base = useFallback ? QString::fromLatin1(kHost)
                                         : QString::fromLatin1(kSearchHost);
        QUrl url = makeQuery(QUrl(QStringLiteral("%1/pc/video/search").arg(base)),
                             !useFallback, sign, tStr);
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(8000);

        QMap<QByteArray, QByteArray> headers;
        if (useFallback) {
            headers = buildMainGatewayHeaders(QDateTime::currentMSecsSinceEpoch());
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
    QNetworkRequest request(QUrl(QStringLiteral("%1/%2").arg(kHost, route)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(12000);
    const auto headers = buildMainGatewayHeaders(QDateTime::currentMSecsSinceEpoch());
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        request.setRawHeader(it.key(), it.value());
    QNetworkReply *reply = nam_.post(request, payload);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback]() {
        const auto error = reply->error();
        const QByteArray body = reply->readAll();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError) { callback(false, {}, errorText); return; }
        callback(true, DmghgClient::decryptBody(body), {});
    });
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
                m.insert(QStringLiteral("vodName"), name);
                m.insert(QStringLiteral("vodPic"), pic);
                m.insert(QStringLiteral("vodRemarks"), remarks);
                m.insert(QStringLiteral("typeName"), strField(o, QStringLiteral("type")));
                m.insert(QStringLiteral("vodYear"), strField(o, QStringLiteral("year")));
                m.insert(QStringLiteral("vodArea"), strField(o, QStringLiteral("area")));
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
                m.insert(QStringLiteral("vodName"), name);
                m.insert(QStringLiteral("vodPic"), pic);
                m.insert(QStringLiteral("vodRemarks"), remarks);
                m.insert(QStringLiteral("typeName"), strField(o, QStringLiteral("type")));
                m.insert(QStringLiteral("vodYear"), strField(o, QStringLiteral("year")));
                m.insert(QStringLiteral("vodArea"), strField(o, QStringLiteral("area")));
                out.append(m);
            }
            callback(true, out, total, {});
        });
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
            m.insert(QStringLiteral("vodName"), strField(d, QStringLiteral("name")));
            m.insert(QStringLiteral("vodPic"), strField(d, QStringLiteral("pic")));
            m.insert(QStringLiteral("vodRemarks"), strField(d, QStringLiteral("continu")));
            m.insert(QStringLiteral("vodYear"), strField(d, QStringLiteral("year")));
            m.insert(QStringLiteral("vodArea"), strField(d, QStringLiteral("area")));
            m.insert(QStringLiteral("typeName"), strField(d, QStringLiteral("type")));
            m.insert(QStringLiteral("vodActor"), strField(d, QStringLiteral("actor")));
            m.insert(QStringLiteral("vodDirector"), strField(d, QStringLiteral("director")));
            m.insert(QStringLiteral("vodContent"), strField(d, QStringLiteral("content")));
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
    const auto [url, headers] = getApiUrl(source, now);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(8000);
    QMap<QByteArray, QByteArray> hdrs = headers;
    hdrs.insert("User-Agent", kUserAgent);
    // 解析网关偶尔会在首次连接时主动关闭连接，增加短间隔重试避免用户必须手动刷新。
    fetchWithRetry(nam_, req, hdrs, 5, 900, [callback](bool ok, const QByteArray &body, const QString &err) {
        if (!ok) { callback(false, {}, err); return; }
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
            m.insert(QStringLiteral("id"), o.value(QStringLiteral("id")).toInt()); m.insert(QStringLiteral("uname"), strField(o, QStringLiteral("uname")));
            m.insert(QStringLiteral("comments"), strField(o, QStringLiteral("comments"))); m.insert(QStringLiteral("createdAt"), strField(o, QStringLiteral("created_at")));
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
                m.insert(QStringLiteral("text"), c.value(QStringLiteral("content")).toString());
                m.insert(QStringLiteral("color"), c.value(QStringLiteral("color")).toVariant().toLongLong());
                m.insert(QStringLiteral("mode"), c.value(QStringLiteral("type")).toInt(0)); // 0=滚动
                m.insert(QStringLiteral("size"), c.value(QStringLiteral("size")).toString(QStringLiteral("M")));
                out.append(m);
            }
            callback(true, out, data.value(QStringLiteral("total")).toInt(), {});
        });
}
