#pragma once

#include <QByteArray>
#include <QStringList>

// 构建期生成的密钥混淆单元 (tools 会话生成, 勿手工编辑)。
// 所有动漫接口密钥以 XOR 密文形态存储, 运行时解密到内存,
// 二进制与源码中均不出现明文, 防止 strings 直接提取。
namespace DmghgSecrets {

QByteArray authKey();        // 主网关 AES-256 鉴权密钥
QByteArray ivKey2();         // 会话 IV 派生 XOR 掩码
QByteArray searchSecret();   // 搜索网关 sign 密钥
QByteArray jxAesKey();       // jx 二次解析 AES key
QByteArray jxAesIv();        // jx 二次解析 AES iv
QByteArray jxAuthCfgKey();   // jx auth 参数密钥
QByteArray jxHdrAuthKey();   // jx X-Goepp-Client-Auth key
QByteArray jxHdrProofKey();  // jx X-Goepp-Client-Proof key
QByteArray jxHdrProbeKey();  // jx X-Goepp-Client-Probe key
QByteArray jxHdrCheckKey();  // jx X-Goepp-Client-Check key
QByteArray shareSecret();    // 站点分享码加密密钥
QStringList officialGatewayZones();  // 官方网关域名区 (域名发现用)
QByteArray rsaPem();         // 内嵌 RSA 私钥 (解密后 PEM, 磁盘不再落明文)

}  // namespace DmghgSecrets
