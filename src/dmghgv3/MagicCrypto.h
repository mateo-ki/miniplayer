#pragma once

#include <QString>
#include <QByteArray>
#include <cstdint>
#include <optional>
#include <vector>

/**
 * *** v1.0.8+ 的 magic_sdk 算法层（纯算法，无网络）。
 *
 * 逐字节移植并验证自 D:\project\ida\dm 的离线复现结果：
 *  - bootstrap 配置藏在服务端返回的 JPEG 尾部隐写里，密钥由 (硬编码种子 + 30 天代数) 派生；
 *  - 业务请求体是 V3 信封（多算法链 + 每小时确定性掩码 + HMAC 完整性标签）；
 *  - X-Signature 是 md5(timestamp + nonce + appId + signSecret)。
 *
 * 六个链上算法核 (TEA/XOR/RC4/ChaCha20/Blowfish/Base64) 都不是教科书实现，
 * 偏离点见 .cpp 注释；Blowfish 的替换 P/S 表在 MagicBlowfishTables.h。
 */
namespace dmghg {

// 信封链节点算法 id（取自 .so 注册表的 SHA-256 指纹折叠值）。
enum MagicAlgId : quint8 {
    AlgMagicXor = 228,       // 8 字节密钥
    AlgMagicRc4 = 25,        // 16 字节密钥
    AlgMagicBase64 = 130,    // 无密钥，改变长度
    AlgMagicTea = 154,       // 16 字节密钥，8 字节分组
    AlgMagicChaCha20 = 59,   // 44 字节 = 32 密钥 + 12 nonce
    AlgMagicBlowfish = 117,  // 16 字节密钥，8 字节分组
};

int keyLengthFor(int algId);

struct BootstrapConfig {
    QString masterSecret;
    QString signSecret;
    QString mbase64Table;      // 64 字符自定义 base64 字母表（指纹头用）
    QString fpHmacSecretHex;   // 64 位十六进制 = 32 字节
    int fpAlgoVersion = 0;
    int keyVersion = 0;
    qint64 configVersion = 0;
    qint64 serverTimeMs = 0;
    bool valid() const { return !masterSecret.isEmpty() && !signSecret.isEmpty(); }
};

// 30 天代数：候选密钥只由它决定。
int bootstrapGeneration(qint64 unixSeconds);

// 某个代数的 64 字节候选密钥 = HMAC-SHA512(key, "bootstrap:" + appId + ":" + gen)。
QByteArray candidateKey(int generation, const QByteArray &appIdUtf8);

// 解析并解密 bootstrap JPEG 尾部隐写；gen 不匹配时返回 nullopt（调用方应邻域重试）。
std::optional<BootstrapConfig> decodeBootstrap(const QByteArray &jpegBytes,
                                               const QByteArray &appIdUtf8,
                                               int generation,
                                               QString *error = nullptr);

// secret = hex(hmac_sha256(key=masterSecret, msg="device:" + deviceId)) 的 64 个 ASCII 字节。
QByteArray deriveSecret(const QString &masterSecret, const QString &deviceId);

// X-Signature。
QString makeSignature(qint64 timestampMs, const QString &nonceUtf8,
                      const QString &appId, const QString &signSecret);

// 用客户端自选链封装一个 V3 信封。plain 必须 <= 64KiB（更大的分块格式未实现）。
QByteArray sealEnvelope(const QByteArray &secret, const QByteArray &plain, qint64 timeSeconds);

// 解开一个 V3 信封（服务端可选任意链，含 Blowfish/Base64）。失败返回 nullopt。
std::optional<QByteArray> openEnvelope(const QByteArray &secret, const QByteArray &envelope,
                                       QString *error = nullptr);

// 每小时确定性掩码与信封完整性标签（导出以便单元测试直接比对参照向量）。
QByteArray wireMask(const QByteArray &secret, qint64 slot);
QByteArray integrityTag(const QByteArray &secret, const QByteArray &envNoTag);

// ── 单节点原语（导出以便单元测试直接比对参照向量）──────────
QByteArray teaEncrypt(const QByteArray &key, const QByteArray &plain);
QByteArray teaDecrypt(const QByteArray &key, const QByteArray &cipher);
QByteArray xorEncrypt(const QByteArray &key, const QByteArray &plain);
QByteArray xorDecrypt(const QByteArray &key, const QByteArray &cipher);
QByteArray rc4(const QByteArray &key, const QByteArray &data);   // 自逆
QByteArray chacha(const QByteArray &key44, const QByteArray &data);
QByteArray bfEncrypt(const QByteArray &key, const QByteArray &plain);
QByteArray bfDecrypt(const QByteArray &key, const QByteArray &cipher);
QByteArray b64Encode(const QByteArray &data);
QByteArray b64Decode(const QByteArray &data);

}  // namespace dmghg
