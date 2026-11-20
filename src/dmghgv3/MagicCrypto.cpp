#include "MagicCrypto.h"
#include "MagicBlowfishTables.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>

#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/evp.h>

#include <cstring>

namespace dmghg {
namespace {

constexpr quint32 M32 = 0xFFFFFFFFu;

// 两段域分隔串: 由 Java 传给 magic_sdk_init 并被写进 .bss, 参与掩码与标签计算。
const char kWireDomain[] = "wire-code-mask";
const char kIntegrityDomain[] = "payload-integrity";
const char kBootstrapPrefix[] = "bootstrap:";

// 引导密钥种子 = BuildConfig 的 api.appconfig.payload0..3；两个 16 字节常量是
// .so .rodata 里的异或掩码 (0x3070 / 0x30E0)，不是密钥。
const char *kAppConfigHex[4] = {"3a881c742789416a", "997ade9f1b84679a",
                                "60b97b4906521e31", "859a046e3fe1b4ab"};
const quint8 kKdfMask0[16] = {0x21, 0xca, 0xab, 0x48, 0x90, 0xce, 0x93, 0xbb,
                              0xb7, 0xa4, 0x23, 0x19, 0x91, 0xff, 0xf4, 0xdd};
const quint8 kKdfMask1[16] = {0xb4, 0x34, 0xed, 0x91, 0x73, 0x03, 0x45, 0xe3,
                              0x30, 0xf1, 0x3b, 0x6a, 0x3c, 0x67, 0x0f, 0xbf};

// 信封里链节点的算法 id 与其密钥长度（id 取自注册表的 SHA-256 指纹折叠值）。
struct ChainNode {
    int id;
    int keyLen;
};

QByteArray hmacHash(const EVP_MD *md, const QByteArray &key, const QByteArray &msg) {
    QByteArray out(EVP_MAX_MD_SIZE, '\0');
    unsigned int len = 0;
    if (!HMAC(md, key.constData(), key.size(),
              reinterpret_cast<const unsigned char *>(msg.constData()),
              static_cast<size_t>(msg.size()),
              reinterpret_cast<unsigned char *>(out.data()), &len))
        return {};
    out.truncate(static_cast<int>(len));
    return out;
}

QByteArray aesCbcDecryptPkcs7(const QByteArray &key, const QByteArray &iv,
                              const QByteArray &cipher) {
    const EVP_CIPHER *sel = key.size() == 32 ? EVP_aes_256_cbc()
                        : key.size() == 16 ? EVP_aes_128_cbc() : nullptr;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    QByteArray out(cipher.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    int written = 0, finalLen = 0;
    const bool ok = sel && ctx
        && EVP_DecryptInit_ex(ctx, sel, nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()),
                              reinterpret_cast<const unsigned char *>(iv.constData())) == 1
        && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &written,
                             reinterpret_cast<const unsigned char *>(cipher.constData()),
                             cipher.size()) == 1
        && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data() + written),
                               &finalLen) == 1;
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    if (!ok) return {};
    out.truncate(written + finalLen);
    return out;
}

quint32 be32(const uchar *p) {
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
}

quint32 le32(const uchar *p) {
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

void putBe32(uchar *p, quint32 v) {
    p[0] = uchar(v >> 24); p[1] = uchar(v >> 16); p[2] = uchar(v >> 8); p[3] = uchar(v);
}

void putLe32(uchar *p, quint32 v) {
    p[0] = uchar(v); p[1] = uchar(v >> 8); p[2] = uchar(v >> 16); p[3] = uchar(v >> 24);
}

quint64 be64v(const QByteArray &b, int off) {
    quint64 v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | uchar(b.at(off + i));
    return v;
}

void appendBe64(QByteArray *b, quint64 v) {
    for (int i = 7; i >= 0; --i) b->append(char((v >> (i * 8)) & 0xFF));
}

void appendBe16(QByteArray *b, uint v) {
    b->append(char((v >> 8) & 0xFF));
    b->append(char(v & 0xFF));
}

QByteArray randomBytes(int n) {
    QByteArray b(n, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char *>(b.data()), n) == 1) return b;
    for (int done = 0; done < n;) {
        const QByteArray mix = QCryptographicHash::hash(
            b + QByteArray::number(done), QCryptographicHash::Sha256);
        const int take = qMin(mix.size(), n - done);
        b.replace(done, take, mix.left(take));
        done += take;
    }
    return b;
}

bool pkcs7Strip(QByteArray *b) {
    if (b->isEmpty()) return false;
    const int p = uchar(b->at(b->size() - 1));
    if (p < 1 || p > 8 || p > b->size()) return false;
    for (int i = b->size() - p; i < b->size(); ++i)
        if (uchar(b->at(i)) != p) return false;
    b->truncate(b->size() - p);
    return true;
}

// ------------------------------------------------------------------ MagicTEA
// 与教科书 TEA 有三处不同: delta = 0xB7E15163、轮函数用 <<5 / >>3、
// 且每轮先更新 L 再更新 R。共 32 轮。
constexpr quint32 kTeaDelta = 0xB7E15163u;
constexpr int kTeaRounds = 32;

quint32 teaF(quint32 y, quint32 sum, quint32 ka, quint32 kb) {
    return ((((y << 5) + ka) & M32) ^ ((y + sum) & M32) ^ (((y >> 3) + kb) & M32)) & M32;
}

void teaBlockEncrypt(quint32 &L, quint32 &R, const quint32 k[4]) {
    quint32 sum = 0;
    for (int i = 0; i < kTeaRounds; ++i) {
        sum = (sum + kTeaDelta) & M32;
        L = (L + teaF(R, sum, k[0], k[1])) & M32;
        R = (R + teaF(L, sum, k[2], k[3])) & M32;
    }
}

void teaBlockDecrypt(quint32 &L, quint32 &R, const quint32 k[4]) {
    quint32 sum = (kTeaDelta * kTeaRounds) & M32;
    for (int i = 0; i < kTeaRounds; ++i) {
        R = (R - teaF(L, sum, k[2], k[3])) & M32;
        L = (L - teaF(R, sum, k[0], k[1])) & M32;
        sum = (sum - kTeaDelta) & M32;
    }
}

void loadTeaKey(const QByteArray &key, quint32 k[4]) {
    for (int i = 0; i < 4; ++i)
        k[i] = be32(reinterpret_cast<const uchar *>(key.constData()) + i * 4);
}

QByteArray teaEncryptPadded(const quint32 k[4], const QByteArray &plain) {
    QByteArray data = plain;
    const int pad = 8 - int(data.size() % 8);
    data.append(QByteArray(pad, char(pad)));
    QByteArray out;
    out.reserve(data.size());
    for (int i = 0; i + 8 <= data.size(); i += 8) {
        const uchar *p = reinterpret_cast<const uchar *>(data.constData()) + i;
        quint32 L = be32(p), R = be32(p + 4);
        teaBlockEncrypt(L, R, k);
        QByteArray blk(8, '\0');
        putBe32(reinterpret_cast<uchar *>(blk.data()), L);
        putBe32(reinterpret_cast<uchar *>(blk.data()) + 4, R);
        out += blk;
    }
    return out;
}

QByteArray teaDecryptStrip(const quint32 k[4], const QByteArray &cipher) {
    QByteArray out;
    out.reserve(cipher.size());
    for (int i = 0; i + 8 <= cipher.size(); i += 8) {
        const uchar *p = reinterpret_cast<const uchar *>(cipher.constData()) + i;
        quint32 L = be32(p), R = be32(p + 4);
        teaBlockDecrypt(L, R, k);
        QByteArray blk(8, '\0');
        putBe32(reinterpret_cast<uchar *>(blk.data()), L);
        putBe32(reinterpret_cast<uchar *>(blk.data()) + 4, R);
        out += blk;
    }
    return pkcs7Strip(&out) ? out : QByteArray();
}

// ------------------------------------------------------------- MagicXor
// 不是重复密钥异或: 带密文反馈与位置项，再做 3 位循环左移；链接初值 0x5A。
quint32 rotl8(quint32 v, int n) { return ((v << n) | (v >> (8 - n))) & 0xFFu; }
quint32 rotr8(quint32 v, int n) { return ((v >> n) | (v << (8 - n))) & 0xFFu; }

// ------------------------------------------------------------- MagicBlowfish
// 结构与教科书一致，但初始 P/S 表被整体替换 (MagicBlowfishTables.h)，
// 且最终子键落在相反的半字上: word1 = R^P[17]、word2 = L^P[16]。
struct BfCtx {
    quint32 P[18];
    quint32 S[4][256];
};

quint32 bfF(quint32 x, const BfCtx &c) {
    return ((((c.S[0][(x >> 24) & 0xFF] + c.S[1][(x >> 16) & 0xFF]) & M32)
             ^ c.S[2][(x >> 8) & 0xFF]) + c.S[3][x & 0xFF]) & M32;
}

void bfBlockEncrypt(quint32 &L, quint32 &R, const BfCtx &c) {
    for (int i = 0; i < 16; ++i) {
        const quint32 t = (c.P[i] ^ L) & M32;
        L = (bfF(t, c) ^ R) & M32;
        R = t;
    }
    const quint32 oL = (R ^ c.P[17]) & M32;
    const quint32 oR = (L ^ c.P[16]) & M32;
    L = oL;
    R = oR;
}

void bfBlockPair(const BfCtx &c, quint32 &a, quint32 &b, bool encrypt) {
    if (encrypt) {
        bfBlockEncrypt(a, b, c);
        return;
    }
    quint32 L = (b ^ c.P[16]) & M32;
    quint32 R = (a ^ c.P[17]) & M32;
    for (int i = 15; i >= 0; --i) {
        const quint32 t = R;
        R = (L ^ bfF(t, c)) & M32;
        L = (t ^ c.P[i]) & M32;
    }
    a = L;
    b = R;
}

BfCtx bfExpand(const QByteArray &key) {
    BfCtx c{};
    for (int i = 0; i < 18; ++i) c.P[i] = kBlowfishP[i];
    for (int i = 0; i < 1024; ++i) c.S[i >> 8][i & 0xFF] = kBlowfishS[i];
    const int nw = qMax(key.size() / 4, 1);
    for (int i = 0; i < 18; ++i)
        c.P[i] ^= be32(reinterpret_cast<const uchar *>(key.constData()) + (i % nw) * 4);
    quint32 L = 0, R = 0;
    for (int i = 0; i < 18; i += 2) {
        bfBlockEncrypt(L, R, c);
        c.P[i] = L;
        c.P[i + 1] = R;
    }
    for (int box = 0; box < 4; ++box) {
        for (int j = 0; j < 256; j += 2) {
            bfBlockEncrypt(L, R, c);
            c.S[box][j] = L;
            c.S[box][j + 1] = R;
        }
    }
    return c;
}

QByteArray bfProcess(const QByteArray &key, const QByteArray &in, bool encrypt) {
    if (key.size() != 16) return {};
    const BfCtx c = bfExpand(key);
    if (encrypt) {
        QByteArray data = in;
        const int pad = 8 - int(data.size() % 8);
        data.append(QByteArray(pad, char(pad)));
        QByteArray out;
        out.reserve(data.size());
        for (int i = 0; i + 8 <= data.size(); i += 8) {
            const uchar *p = reinterpret_cast<const uchar *>(data.constData()) + i;
            quint32 a = be32(p), b = be32(p + 4);
            bfBlockPair(c, a, b, true);
            QByteArray blk(8, '\0');
            putBe32(reinterpret_cast<uchar *>(blk.data()), a);
            putBe32(reinterpret_cast<uchar *>(blk.data()) + 4, b);
            out += blk;
        }
        return out;
    }
    if (in.isEmpty() || in.size() % 8) return {};
    QByteArray out;
    out.reserve(in.size());
    for (int i = 0; i + 8 <= in.size(); i += 8) {
        const uchar *p = reinterpret_cast<const uchar *>(in.constData()) + i;
        quint32 a = be32(p), b = be32(p + 4);
        bfBlockPair(c, a, b, false);
        QByteArray blk(8, '\0');
        putBe32(reinterpret_cast<uchar *>(blk.data()), a);
        putBe32(reinterpret_cast<uchar *>(blk.data()) + 4, b);
        out += blk;
    }
    return pkcs7Strip(&out) ? out : QByteArray();
}

// ----------------------------------------------------------- MagicChaCha20
// sigma 换成 "mAgic CHACha ROu"，旋转量由 16/12/8/7 改成 15/11/9/6，计数器从 1 起。
// 除此之外与 RFC 8439 一致：块状态和 feed-forward 都用递增至计数器。
// （46 条向量最长 90 字节，块 0 在两种规则下逐字节相同，所以多块规则是用服务端的
//  JSON 解析器当预言机定下来的，见 D:\project\ida\dm\scripts\chacha_rule_oracle.py。）
const quint32 kChaSigma[4] = {0x6967416Du, 0x48432063u, 0x61684341u, 0x754F5220u};
const int kChaRot[4] = {15, 11, 9, 6};

quint32 rotl32(quint32 v, int n) { return ((v << n) | (v >> (32 - n))) & M32; }

void chachaQuarter(quint32 s[16], int a, int b, int c, int d) {
    s[a] = (s[a] + s[b]) & M32; s[d] = rotl32(s[d] ^ s[a], kChaRot[0]);
    s[c] = (s[c] + s[d]) & M32; s[b] = rotl32(s[b] ^ s[c], kChaRot[1]);
    s[a] = (s[a] + s[b]) & M32; s[d] = rotl32(s[d] ^ s[a], kChaRot[2]);
    s[c] = (s[c] + s[d]) & M32; s[b] = rotl32(s[b] ^ s[c], kChaRot[3]);
}

// ---------------------------------------------------------- MagicBase64
// 信封节点用 .so 内置字母表（既不是标准表，也不是服务端下发给指纹头的那张）。
const char kB64Alphabet[] = "ghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+/abcdef";

int b64ReverseIndex(uchar c) {
    const char *p = static_cast<const char *>(memchr(kB64Alphabet, char(c), 64));
    return p ? int(p - kB64Alphabet) : -1;   // 表外字符按有符号 -1 直接参与运算
}

QByteArray nodeApply(int id, const QByteArray &key, const QByteArray &data, bool encrypt) {
    switch (id) {
    case AlgMagicTea: {
        if (key.size() != 16) return {};
        quint32 k[4];
        loadTeaKey(key, k);
        return encrypt ? teaEncryptPadded(k, data) : teaDecryptStrip(k, data);
    }
    case AlgMagicXor:
        return encrypt ? xorEncrypt(key, data) : xorDecrypt(key, data);
    case AlgMagicRc4:
        return rc4(key, data);
    case AlgMagicChaCha20:
        return chacha(key, data);
    case AlgMagicBlowfish:
        return bfProcess(key, data, encrypt);
    case AlgMagicBase64:
        return encrypt ? b64Encode(data) : b64Decode(data);
    default:
        return {};
    }
}

}  // namespace

// ── 掩码与标签：都只以 secret(+小时槽) 为输入，因此每小时确定不变 ──────────
QByteArray wireMask(const QByteArray &secret, qint64 slot) {
    QByteArray msg;
    appendBe64(&msg, quint64(slot));
    msg += QByteArray(kWireDomain);
    return hmacHash(EVP_sha256(), secret, msg);
}

QByteArray integrityTag(const QByteArray &secret, const QByteArray &envNoTag) {
    return hmacHash(EVP_sha256(), secret, QByteArray(kIntegrityDomain) + envNoTag);
}

int keyLengthFor(int algId) {
    switch (algId) {
    case AlgMagicXor: return 8;
    case AlgMagicRc4: return 16;
    case AlgMagicTea: return 16;
    case AlgMagicChaCha20: return 44;
    case AlgMagicBlowfish: return 16;
    case AlgMagicBase64: return 0;
    default: return -1;
    }
}

int bootstrapGeneration(qint64 unixSeconds) {
    return int(unixSeconds / 2592000);   // 30 天一代
}

QByteArray candidateKey(int generation, const QByteArray &appIdUtf8) {
    QByteArray seed;
    for (int i = 0; i < 4; ++i)
        seed += QByteArray::fromHex(QByteArray(kAppConfigHex[i]));
    QByteArray key(32, '\0');
    for (int i = 0; i < 16; ++i) {
        key[i] = char(uchar(seed.at(i)) ^ kKdfMask0[i]);
        key[16 + i] = char(uchar(seed.at(16 + i)) ^ kKdfMask1[i]);
    }
    const QByteArray msg = QByteArray(kBootstrapPrefix) + appIdUtf8 + ":"
                         + QByteArray::number(generation);
    return hmacHash(EVP_sha512(), key, msg);
}

std::optional<BootstrapConfig> decodeBootstrap(const QByteArray &jpeg, const QByteArray &appId,
                                               int generation, QString *error) {
    const int eoi = jpeg.lastIndexOf("\xff\xd9");
    if (eoi < 0) {
        if (error) *error = QStringLiteral("bootstrap JPEG 无 EOI 标记");
        return std::nullopt;
    }
    const QByteArray tail = jpeg.mid(eoi + 2);
    if (tail.size() < 56 || tail.left(4) != QByteArray::fromHex("89504347")) {
        if (error) *error = QStringLiteral("bootstrap 隐写 magic 不匹配");
        return std::nullopt;
    }
    const uint payloadLen = (uint(be32(reinterpret_cast<const uchar *>(tail.constData()) + 4)));
    if (tail.size() < int(8 + payloadLen + 32)) {
        if (error) *error = QStringLiteral("bootstrap 载荷不完整");
        return std::nullopt;
    }
    const QByteArray iv = tail.mid(8, 16);
    const QByteArray cipher = tail.mid(24, int(payloadLen));
    const QByteArray mac = tail.mid(24 + int(payloadLen), 32);

    const QByteArray k = candidateKey(generation, appId);
    if (hmacHash(EVP_sha256(), k.mid(32, 32), iv + cipher) != mac) {
        if (error) *error = QStringLiteral("bootstrap HMAC 校验失败 (gen=%1)").arg(generation);
        return std::nullopt;
    }
    const QByteArray plain = aesCbcDecryptPkcs7(k.mid(0, 32), iv, cipher);
    const QJsonDocument doc = QJsonDocument::fromJson(plain);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("bootstrap JSON 解析失败");
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    BootstrapConfig cfg;
    cfg.masterSecret = o.value(QStringLiteral("masterSecret")).toString();
    cfg.signSecret = o.value(QStringLiteral("signSecret")).toString();
    cfg.keyVersion = o.value(QStringLiteral("keyVersion")).toInt();
    cfg.configVersion = qint64(o.value(QStringLiteral("configVersion")).toDouble());
    cfg.serverTimeMs = qint64(o.value(QStringLiteral("serverTime")).toDouble());
    const QJsonObject mb = o.value(QStringLiteral("mbase64Table")).toObject();
    cfg.mbase64Table = mb.value(QStringLiteral("tableStr")).toString();
    const QJsonObject fp = o.value(QStringLiteral("fpAlgo")).toObject();
    cfg.fpHmacSecretHex = fp.value(QStringLiteral("hmacSecret")).toString();
    cfg.fpAlgoVersion = fp.value(QStringLiteral("version")).toInt();
    if (!cfg.valid()) {
        if (error) *error = QStringLiteral("bootstrap 缺少 masterSecret/signSecret");
        return std::nullopt;
    }
    return cfg;
}

QByteArray deriveSecret(const QString &masterSecret, const QString &deviceId) {
    const QByteArray raw = hmacHash(EVP_sha256(), masterSecret.toUtf8(),
                                    QByteArray("device:") + deviceId.toUtf8());
    return raw.toHex();   // 64 个 ASCII 十六进制字符，即信封用的 secret
}

QString makeSignature(qint64 timestampMs, const QString &nonce, const QString &appId,
                      const QString &signSecret) {
    const QByteArray joined = QByteArray::number(timestampMs) + nonce.toUtf8()
                            + appId.toUtf8() + signSecret.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(joined, QCryptographicHash::Md5).toHex());
}

QByteArray sealEnvelope(const QByteArray &secret, const QByteArray &plain, qint64 timeSeconds) {
    static const ChainNode kChain[3] = {{AlgMagicTea, 16}, {AlgMagicXor, 8}, {AlgMagicRc4, 16}};
    constexpr int kNodeCount = 3;
    if (plain.size() > 65536) return {};   // >64KiB 的分块格式未实现

    QByteArray keys[kNodeCount];
    for (int i = 0; i < kNodeCount; ++i) keys[i] = randomBytes(kChain[i].keyLen);

    const qint64 slot = timeSeconds / 3600;
    const QByteArray m = wireMask(secret, slot);

    QByteArray spec;
    spec.append(char(kNodeCount));
    for (int i = 0; i < kNodeCount; ++i)
        spec.append(char(uchar(kChain[i].id) ^ uchar(m.at(i % 32))));

    QByteArray blob;
    for (int i = 0; i < kNodeCount; ++i) {
        appendBe16(&blob, quint32(keys[i].size()));
        blob += keys[i];
    }

    QByteArray env;
    env.append(char(3));
    env.append(char(0));
    appendBe64(&env, quint64(slot));
    env.append(char(spec.size()));
    env += spec;
    appendBe16(&env, quint32(blob.size()));
    env += blob;

    QByteArray body = plain;
    for (int i = 0; i < kNodeCount; ++i)
        body = nodeApply(kChain[i].id, keys[i], body, true);
    if (body.isNull()) return {};
    env += body;
    env += integrityTag(secret, env);
    return env;
}

std::optional<QByteArray> openEnvelope(const QByteArray &secret, const QByteArray &env,
                                       QString *error) {
    if (env.size() < 45) {
        if (error) *error = QStringLiteral("信封过短");
        return std::nullopt;
    }
    if (uchar(env.at(0)) != 3) {
        if (error) *error = QStringLiteral("信封版本非 v3: %1").arg(uchar(env.at(0)));
        return std::nullopt;
    }
    const bool chunked = uchar(env.at(1)) != 0;   // bit0: 正文 >64KiB，改走分块旁缓冲
    if (uchar(env.at(1)) > 1) {
        if (error) *error = QStringLiteral("未知信封标志: %1").arg(uchar(env.at(1)));
        return std::nullopt;
    }
    if (integrityTag(secret, env.left(env.size() - 32)) != env.right(32)) {
        if (error) *error = QStringLiteral("信封 HMAC 校验失败");
        return std::nullopt;
    }
    const qint64 slot = qint64(be64v(env, 2));
    const int specLen = uchar(env.at(10));
    if (specLen < 2 || env.size() - 32 < 13 + specLen) {
        if (error) *error = QStringLiteral("信封 spec 越界");
        return std::nullopt;
    }
    const QByteArray spec = env.mid(11, specLen);
    const int count = uchar(spec.at(0));
    if (count < 1 || count > 16 || specLen != count + 1) {
        if (error) *error = QStringLiteral("信封节点数非法: %1").arg(count);
        return std::nullopt;
    }
    const int blobAt = 11 + specLen;
    const int blobLen = (uchar(env.at(blobAt)) << 8) | uchar(env.at(blobAt + 1));
    const int cipherAt = blobAt + 2 + blobLen;
    if (env.size() - 32 < cipherAt) {
        if (error) *error = QStringLiteral("信封密钥块越界");
        return std::nullopt;
    }
    const QByteArray blob = env.mid(blobAt + 2, blobLen);
    const QByteArray cipher = env.mid(cipherAt, env.size() - 32 - cipherAt);

    const QByteArray m = wireMask(secret, slot);
    QVector<int> ids;
    QVector<QByteArray> keys;
    for (int i = 0; i < count; ++i)
        ids.append(int(uchar(spec.at(1 + i)) ^ uchar(m.at(i % 32))));
    int p = 0;
    while (p + 2 <= blob.size() && keys.size() < count) {
        const int l = (uchar(blob.at(p)) << 8) | uchar(blob.at(p + 1));
        if (p + 2 + l > blob.size()) break;
        keys.append(blob.mid(p + 2, l));
        p += 2 + l;
    }
    if (keys.size() != count) {
        if (error) *error = QStringLiteral("信封密钥记录不足");
        return std::nullopt;
    }
    auto openChain = [&](const QByteArray &in) -> QByteArray {
        QByteArray body = in;
        for (int i = count - 1; i >= 0; --i) {
            const int len = keyLengthFor(ids[i]);
            if (len < 0 || keys[i].size() != len) return {};
            body = nodeApply(ids[i], keys[i], body, false);
            if (body.isNull()) return {};
        }
        return body;
    };

    if (!chunked) {
        const QByteArray body = openChain(cipher);
        if (body.isNull() && !cipher.isEmpty()) {
            if (error) *error = QStringLiteral("节点解密失败(填充或格式非法)");
            return std::nullopt;
        }
        return body;
    }

    // 分块旁缓冲: "ECSM" + le32(256) + be32(chunkCount) + 每块 {be32 len, 密文}。
    // 每块独立走链、独立补位，所以必须逐块解密后再拼接（真实样本: comment/list 82668B）。
    if (cipher.size() < 12 || std::memcmp(cipher.constData(), "ECSM", 4) != 0) {
        if (error) *error = QStringLiteral("分块信封魔数不符");
        return std::nullopt;
    }
    quint32 chunkCount = 0;
    for (int i = 0; i < 4; ++i)
        chunkCount = (chunkCount << 8) | uchar(cipher.at(8 + i));
    QByteArray body;
    int at = 12;
    for (quint32 i = 0; i < chunkCount; ++i) {
        if (at + 4 > cipher.size()) {
            if (error) *error = QStringLiteral("分块头越界");
            return std::nullopt;
        }
        int len = 0;
        for (int j = 0; j < 4; ++j)
            len = (len << 8) | uchar(cipher.at(at + j));
        at += 4;
        if (len < 0 || at + len > cipher.size()) {
            if (error) *error = QStringLiteral("分块长度越界");
            return std::nullopt;
        }
        const QByteArray part = openChain(cipher.mid(at, len));
        if (part.isNull()) {
            if (error) *error = QStringLiteral("分块 %1 解密失败").arg(i);
            return std::nullopt;
        }
        body += part;
        at += len;
    }
    if (at != cipher.size()) {
        if (error) *error = QStringLiteral("分块尾部残留 %1 字节").arg(cipher.size() - at);
        return std::nullopt;
    }
    return body;
}

// ── 单节点原语 ────────────────────────────────────────────
QByteArray teaEncrypt(const QByteArray &key, const QByteArray &plain) {
    if (key.size() != 16) return {};
    quint32 k[4];
    loadTeaKey(key, k);
    return teaEncryptPadded(k, plain);
}

QByteArray teaDecrypt(const QByteArray &key, const QByteArray &cipher) {
    if (key.size() != 16 || cipher.isEmpty() || cipher.size() % 8) return {};
    quint32 k[4];
    loadTeaKey(key, k);
    return teaDecryptStrip(k, cipher);
}

QByteArray xorEncrypt(const QByteArray &key, const QByteArray &plain) {
    if (key.size() != 8) return {};
    QByteArray out;
    out.reserve(plain.size());
    int prev = 0x5A;
    for (int i = 0; i < plain.size(); ++i) {
        const int c = int(rotl8(uchar(key.at(i & 7)) ^ uchar(plain.at(i))
                                ^ quint32((i + prev) & 0xFF), 3));
        out.append(char(c));
        prev = c;
    }
    return out;
}

QByteArray xorDecrypt(const QByteArray &key, const QByteArray &cipher) {
    if (key.size() != 8) return {};
    QByteArray out;
    out.reserve(cipher.size());
    int prev = 0x5A;
    for (int i = 0; i < cipher.size(); ++i) {
        const int c = uchar(cipher.at(i));
        out.append(char(uchar(key.at(i & 7)) ^ uchar(rotr8(quint32(c), 3))
                        ^ uchar((i + prev) & 0xFF)));
        prev = c;
    }
    return out;
}

QByteArray rc4(const QByteArray &key, const QByteArray &data) {
    if (key.size() != 16) return {};
    uchar S[256];
    for (int i = 0; i < 256; ++i) S[i] = uchar((i + 13) & 0xFF);
    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + (i ^ 0xAA) + S[i] + uchar(key.at(i & 15))) & 0xFF;
        std::swap(S[i], S[j]);
    }
    int x = 0x11, y = 0x22;
    QByteArray out;
    out.reserve(data.size());
    for (int n = 0; n < data.size(); ++n) {
        x = (x + y + 1) & 0xFF;
        const int t = S[x];
        y = (y + t) & 0xFF;
        std::swap(S[x], S[y]);
        out.append(char(uchar(data.at(n)) ^ S[((S[x] + t) ^ uchar(key.at(n & 15))) & 0xFF]));
    }
    return out;
}

QByteArray chacha(const QByteArray &key44, const QByteArray &data) {
    if (key44.size() != 44 || data.isEmpty()) return {};
    quint32 st0[16];
    for (int i = 0; i < 4; ++i) st0[i] = kChaSigma[i];
    for (int i = 0; i < 8; ++i)
        st0[4 + i] = le32(reinterpret_cast<const uchar *>(key44.constData()) + i * 4);
    st0[12] = 1;
    for (int i = 0; i < 3; ++i)
        st0[13 + i] = le32(reinterpret_cast<const uchar *>(key44.constData()) + 32 + i * 4);

    QByteArray ks;
    ks.reserve(((data.size() + 63) / 64) * 64);
    quint32 ctr = 1;
    while (ks.size() < data.size()) {
        quint32 st[16];
        std::memcpy(st, st0, sizeof(st));
        st[12] = ctr;
        quint32 s[16];
        std::memcpy(s, st, sizeof(s));
        for (int r = 0; r < 10; ++r) {
            chachaQuarter(s, 0, 4, 8, 12); chachaQuarter(s, 1, 5, 9, 13);
            chachaQuarter(s, 2, 6, 10, 14); chachaQuarter(s, 3, 7, 11, 15);
            chachaQuarter(s, 0, 5, 10, 15); chachaQuarter(s, 1, 6, 11, 12);
            chachaQuarter(s, 2, 7, 8, 13); chachaQuarter(s, 3, 4, 9, 14);
        }
        uchar blk[64];
        for (int i = 0; i < 16; ++i)
            putLe32(blk + i * 4, (s[i] + st[i]) & M32);
        ks.append(reinterpret_cast<const char *>(blk), 64);
        ctr = (ctr + 1) & M32;
    }
    QByteArray out(data.size(), '\0');
    for (int i = 0; i < data.size(); ++i)
        out[i] = char(uchar(data.at(i)) ^ uchar(ks.at(i)));
    return out;
}

QByteArray bfEncrypt(const QByteArray &key, const QByteArray &plain) {
    return bfProcess(key, plain, true);
}

QByteArray bfDecrypt(const QByteArray &key, const QByteArray &cipher) {
    return bfProcess(key, cipher, false);
}

QByteArray b64Encode(const QByteArray &data) {
    QByteArray out;
    out.reserve((data.size() + 2) / 3 * 4 + 4);
    const int n = data.size();
    int i = 0;
    while (i + 3 <= n) {
        const quint32 v = (uchar(data.at(i)) << 16) | (uchar(data.at(i + 1)) << 8)
                        | uchar(data.at(i + 2));
        out.append(kB64Alphabet[(v >> 18) & 63]);
        out.append(kB64Alphabet[(v >> 12) & 63]);
        out.append(kB64Alphabet[(v >> 6) & 63]);
        out.append(kB64Alphabet[v & 63]);
        i += 3;
    }
    if (n - i == 1) {
        const quint32 v = quint32(uchar(data.at(i))) << 16;
        out.append(kB64Alphabet[(v >> 18) & 63]);
        out.append(kB64Alphabet[(v >> 12) & 63]);
    } else if (n - i == 2) {
        const quint32 v = (uchar(data.at(i)) << 16) | (uchar(data.at(i + 1)) << 8);
        out.append(kB64Alphabet[(v >> 18) & 63]);
        out.append(kB64Alphabet[(v >> 12) & 63]);
        out.append(kB64Alphabet[(v >> 6) & 63]);
    }
    return out;
}

QByteArray b64Decode(const QByteArray &data) {
    QByteArray out;
    const int n = data.size();
    int i = 0;
    while (i + 4 <= n) {
        const qint64 v = (qint64(b64ReverseIndex(uchar(data.at(i)))) << 18)
                       | (qint64(b64ReverseIndex(uchar(data.at(i + 1)))) << 12)
                       | (qint64(b64ReverseIndex(uchar(data.at(i + 2)))) << 6)
                       | qint64(b64ReverseIndex(uchar(data.at(i + 3))));
        out.append(char((v >> 16) & 0xFF));
        out.append(char((v >> 8) & 0xFF));
        out.append(char(v & 0xFF));
        i += 4;
    }
    if (n - i == 3) {
        const qint64 v = (qint64(b64ReverseIndex(uchar(data.at(i)))) << 18)
                       | (qint64(b64ReverseIndex(uchar(data.at(i + 1)))) << 12)
                       | (qint64(b64ReverseIndex(uchar(data.at(i + 2)))) << 6);
        out.append(char((v >> 16) & 0xFF));
        out.append(char((v >> 8) & 0xFF));
    } else if (n - i == 2) {
        const qint64 v = (qint64(b64ReverseIndex(uchar(data.at(i)))) << 18)
                       | (qint64(b64ReverseIndex(uchar(data.at(i + 1)))) << 12);
        out.append(char((v >> 16) & 0xFF));
    }
    return out;
}

}  // namespace dmghg
