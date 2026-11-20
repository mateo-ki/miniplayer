# 动漫（Dmghg）新协议客户端 —— v1.0.8+ magic_sdk

旧算法链路（`bkbfdm.hzhcbkj.cn/pc/*` + AES-256-CBC 时间桶 IV + RSA 响应）完整保留在
`src/media/DmghgClient.*`，本目录是**新增**的新协议实现，不替换、不删除旧代码。

## 文件

| 文件 | 内容 |
|---|---|
| `MagicCrypto.h/.cpp` | bootstrap 隐写解码、候选密钥 KDF、secret 派生、X-Signature、V3 信封 seal/open |
| `MagicBlowfishTables.h` | MagicBlowfish 的替换 P/S 表（从 `libmagic_cipher.so` .rodata 0x3130/0x3178 逐字节导出） |
| `DmghgClientV3.h/.cpp` | 新协议业务客户端（search / listCategories / listVideos / videoDetail / playVideo / parseSource / comments / danmaku / createComment） |
| `DmghgProtocolSwitch.h` | 新旧切换：`DMGHG_PROTOCOL=old\|new` 或 QSettings `dmghg/protocol`，默认走新 |
| `MagicCryptoSelfTest.cpp` | 17 项断言，全部使用真实抓包向量（独立目标 `miniPlayerMagicTests`） |

## 已验证（`miniPlayerMagicTests`，17/17 通过）

```
wireMask / integrityTag 与真实信封互证           ok
openEnvelope 解出真实请求明文 (TEA/XOR/RC4)      ok
openEnvelope 解出含 Blowfish+Base64 链的真实响应  ok
sealEnvelope -> openEnvelope 往返一致             ok
六个算法核各自往返/自逆 + ChaCha 4 块参照                    ok x7
decodeBootstrap 解出真实 masterSecret/signSecret  ok
deriveSecret == app 运行时真实 secret             ok
makeSignature 复现 app 真实 X-Signature           ok
```

期望值全部来自真实 app 数据（`D:\project\ida\dm\logs\myuko\`），其中
"含 Blowfish+Base64 链的真实响应"是**不在参照向量集里**的一条新抓取服务端响应，
2647 字节信封解出 1912 字节 JSON 并能逐字节重新加密回去。

## 关键事实（写死在代码里的东西的来源）

- 网关 `https://rgxbm.xiaoxiangkj.com` 不在 APK 里，由 app 的 addressing 层运行时解析；
  它来自设备 `files/addressing/<dni>/envelope` 的 `polyvision` 组。可用 QSettings
  `dmghg/apiHostV3` 覆盖，官方换域名不必重编译。
- `bootstrap.jpg` **必须带 `X-Device-Id`**：不带时服务端返回一张通用诱饵图（2577B，
  隐写载荷用任何代际密钥都解不开）；带上后返回真配置（2737B）。
- `X-Signature = md5(timestamp + nonce + appId + signSecret)`，免签路径见 `BootstrapClient`。
- 4K 在 `resolve/qualities` 里是 `isVipOnly=true`，直接 `exchange quality=4k` 被
  `code 4032 该清晰度需要开通会员` 拒绝；但免费档 `exchange` 返回的
  `originalUrl = new-<32hex>` 标识经 jx 层会一并给出 4K 直链（实测 889MB H265 MP4，
  Range 请求 206）。所以 **jx 二次解析层没变**，本类直接组合复用 `DmghgClient::parseSource`。
- 新版视频 id 是 19 位雪花号，超出 int 和 JS 双精度安全范围，因此 `vodId` 一律是
  `QString`；传短纯数字会走 `video/idMapping` 自动换成新 id。

## ~~待解项~~（已解决，2026-09-22）

**真正的原因只有一个**：`MagicChaCha20` 的多块 feed-forward 写错了规则 ——
第 12 个字（块计数）在收尾相加时该用**本块递增后的计数器**，实现里用了**恒定的初始计数器**。
两种规则在块 0 完全一致，只在第 2 块起的第 48 字节分道，所以 46 条参照向量（最长 90 字节）
一条都测不出来，而一旦服务端回包的链里含 ChaCha 就必然坏。

之前那份"Qt6 把这个网关的 POST 响应少读并夹脏字节"的诊断是**误诊**：观测到的
`got=1843 cl=2554`、"offset 182 少了引号"都发生在同一封本来就被解坏的正文上，
长度差也只是拿信封长度和明文长度对比。判据其实一开始就在数据里 ——
差异位置**全部 ≡ 48 (mod 64)**，一看ChaCha 块结构就该认出来。

服务端回包的链 = `deriveCode(deviceSecret, 墙上时钟小时槽)`，每小时换一次，
所以这个 bug 是**按小时发作**的：同一天 16:43 三连败、17:00 起全绿，跟 http/https 无关
（诊断期间试过的 `Http2AllowedAttribute=false` + `Connection: close` 保留下来，无害；
网关已回退成 `https://`）。

规则不是靠猜的，是拿服务端自己当预言机定的：`D:\project\ida\dm\scripts\chacha_rule_oracle.py`
把一个 119 字节、第 112 字节正好是 JSON 逗号的请求体按两种规则各加密发一次 ——
`running-ctr → code 200`，`const-ctr → code 400`。

修复后的验证：
- `miniPlayerMagicTests` 17/17（新增一条 4 块 200B 的 ChaCha 参照断言，旧实现必挂）；
- C++ 与 Python 对同一份真实信封逐字节一致（`build/e2e/envelope_check.cpp`）；
- E2E（独立 cl 编译、真实网关 https）：search → videoDetail(12 集) → playVideo →
  `new-1df22e81…` → jx → 4K m3u8 + 1080P mp4，Range 都是 206；
- app 内：`type/listActive` + `video/categoryList` code=200，日志再无 `非 JSON`。

顺带补上的第二件事：**分块信封（flag=1，明文 >64KiB）之前直接拒绝**，
`comment/list` 一拉 200 条就撞上（`暂不支持 >64KiB 的分块信封`）。
框架是 `"ECSM" + le32(256) + be32(块数) + 每块{be32 len, 密文}`，
**每块独立走链、独立补位**，必须逐块解密再拼接。真实样本 82800B 信封 → 82668B 明文，
C++/Python 双向对齐。

## 接线（已完成，2026-09-22）

- `src/media/DmghgClient.h`：8 个业务方法加 `virtual`（唯一对旧文件的改动，纯增量）。
- `DmghgClientV3` 继承 `DmghgClient`，**同签名覆盖**全部业务方法；上层零改动。
  新 id（19 位雪花号）通过会话级句柄表映射成 int（句柄从 1e9 起，不与真实旧 id 撞号）；
  未登记的短 int 走 `video/idMapping` 换新 id 并把原值登记为句柄。
- `DmghgAnimeModel::client()` 按 `dmghg::useV3()` 二选一；
  新增 `Q_PROPERTY(bool useNewApi ...)` / `setUseNewApi(bool)`（写 QSettings `dmghg/protocol`，
  切换时销毁旧客户端并 `clear()`）。
- `ui/qml/components/AnimeView.qml` 工具栏新增"新算法/旧算法"下拉（protocolFilter）。
- CMakeLists 两个目标（miniPlayerModels / miniPlayerCoreTests）注册了
  `src/dmghgv3/MagicCrypto.cpp` 与 `DmghgClientV3.cpp`。
- `MeloBox.exe` 全量编译通过；miniPlayerCoreTests EXITCODE=0。

**V3 模式的已知简化**：`channel` 参数按分类序号映射（QML 里旧频道的数字 id 如 20/21
在新协议无对应），`type`（类型筛选）暂不透传（新接口用 `filters` 字段，格式未实测）；
两者都会退化为第一个分类的列表。评论的 `episodeNum` 取最近一次播放的集号（旧签名无集号参数）。
