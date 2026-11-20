#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <functional>

class QNetworkReply;

/**
 * Dmghg (***) 直连接口客户端。
 *
 * 协议移植自 D:\\project\\ida\\dm 下的 dmghg_api.py / jx_parse.py:
 *  - 主网关请求带 AES-256-CBC 鉴权头 + RSA-2048 解密响应。
 *  - 搜索走独立网关,带 md5 sign + t 参数。
 *  - play 接口返回的 source 标识需要二次解析(jx 解析网关, 域名会随官方
 *    更换而调整, 见 .cpp 的 kJxEndpoints),
 *    解析链路涉及 AES-128-CBC、md5 三层 X-Goepp 头与 URL 改写。
 *
 * 所有网络请求异步执行,结果通过回调在主线程返回,避免阻塞 UI。
 */
class DmghgClient : public QObject {
    Q_OBJECT
public:
    explicit DmghgClient(QObject *parent = nullptr);

    // ── 业务接口 ──────────────────────────────────────
    // 关键词搜索。回调返回 (ok, itemsJsonArray, errorMessage)。
    virtual void search(const QString &keyword, int page, int limit,
                const std::function<void(bool, const QVariantList &, const QString &)> &callback);

    // 视频列表。channel=0 默认。
    virtual void listVideos(int channel, int page, int limit, const QString &sort, const QString &type,
                    const std::function<void(bool, const QVariantList &, int total, const QString &)> &callback);

    // 列表页两个筛选下拉的数据源，各自一组 [{id,name}]。旧协议没有对应接口，
    // 基类回空表，界面沿用内置常量。
    using FilterCb = std::function<void(bool, const QVariantList &options, const QString &err)>;
    virtual void loadChannelOptions(const FilterCb &callback);
    virtual void loadTypeOptions(const FilterCb &callback);

    // 每周更新排播（周一..周日，key="1".."7"）。旧协议没有对应接口，基类回空。
    virtual void loadWeeklySchedule(
        const std::function<void(bool, const QVariantMap &days, const QString &err)> &callback);

    // 视频详情,返回元信息 + parts 分集列表。
    virtual void videoDetail(int vid,
                     const std::function<void(bool, const QVariantMap &, const QString &)> &callback);

    /// 把「我的」页存下来的字符串编号换成 videoDetail 能用的 int。
    /// 返回 <=0 表示当前协议打不开这条记录（例如旧协议遇到 18 位新编号）。
    virtual int handleForKey(const QString &key);

    // 播放列表,返回 play 接口的 data[0].url 源标识。
    virtual void playVideo(int vid, const QString &part, const QString &playSource,
                   const std::function<void(bool, const QString &source, const QString &)> &callback);

    // 二次解析: source 标识 -> 真实流地址列表。每条含 url/type/name。
    virtual void parseSource(const QString &source,
                     const std::function<void(bool, const QVariantList &items, const QString &)> &callback);

    virtual void comments(int vid, int page, int limit,
                  const std::function<void(bool, const QVariantList &items, int total, const QString &)> &callback);
    virtual void createComment(const QString &comment, const QString &uuid, const QString &dots,
                       const std::function<void(bool, const QString &)> &callback);

    // 站内弹幕(按毫秒时间窗口)。移植自 dmghg_api.py::get_danmaku_internal。
    // vid 视频ID, part 集名, play 播放源(空则"cn"), startMs/endMs 窗口起止(毫秒)。
    // 回调返回 (ok, items[{id,text,color,timePoint,mode,size}], total, err)。
    // items[].content 是 JSON 字符串,这里二次 parse 出 color/content/size/type。
    virtual void danmaku(int vid, const QString &part, const QString &play, int startMs, int endMs,
                 const std::function<void(bool, const QVariantList &items, int total, const QString &)> &callback);

private:
    QNetworkAccessManager nam_;

    // 主网关请求:返回原始密文响应体。
    void callMainGateway(const QString &route, qint64 nowMs,
                          const std::function<void(bool, const QByteArray &body, const QString &err)> &callback);
    // 搜索网关请求。
    void callSearchGateway(const QString &keyword, int page, int limit, qint64 nowSec,
                           const std::function<void(bool, const QByteArray &body, const QString &err)> &callback);
    void callMainPost(const QString &route, const QByteArray &payload,
                      const std::function<void(bool, const QByteArray &body, const QString &err)> &callback);

    // 后台时间桶恢复:派生 IV 被拒 (403501/30000) 时触发。
    // 先用 HTTP Date 校准时钟并做 ±1 桶探测(真正的桶错位只可能来自时钟漂移);
    // 均未命中则判定为服务端 appid 级瞬时风控(约 3 分钟自愈),进入全局静默期
    // (入口闸门拦截新请求),到期单探测复查,通过后自动重放排队请求。
    // 异步、不阻塞当前请求;不做多主机故障转移,避免无效请求风暴延长风控窗口。
    void recoverSessionIv();

    // 网关域名发现: 拉取当前网关的 pc/config, 从响应里扫描官方域名的 URL,
    // 逐个探测 (/pc/config 返回 20000) 后持久化, 参与后续候选。
    // 官方换域名时 (旧域名 DNS 失效) 只要还有任一可用候选即可自动跟进。
    void discoverGatewayHosts();
    void probeGatewayHost(const QString &host);

    // 官方引导端点 (/app/config/host) 异步探测与网关更新
    void queryBootstrapGateway();
    // 异步通过 DoH (223.5.5.5) 解析域名最新 CDN 节点 IP
    void resolveGatewayByDoh(const QString &domain);

    // 解析失败响应体:明文 JSON 直接返回,密文走 RSA+AES。
    static QByteArray decryptBody(const QByteArray &body);

    // jx 解析链路内部方法
    static QByteArray aes128CbcDecrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &base64Data);
    static QByteArray aes128CbcEncrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &plain);

    QPointer<QNetworkReply> primaryReply_; // 取消用(单活跃主请求)

    // 会话失效期间失败的请求队列: 恢复成功后自动重放, 用户无需手动刷新。
    struct PendingGatewayCall {
        QString route;
        std::function<void(bool, const QByteArray &, const QString &)> callback;
    };
    QList<PendingGatewayCall> pendingSessionReplays_;

    // 网关域名发现进行中标记: 首次成功 / 切换成功后各跑一轮, 防重复扫描。
    bool gatewayDiscoveryDone_ = false;

    // 后台恢复并发护栏:同一时刻只允许一个 recoverSessionIv 在跑,
    // 防止多个失败请求各自启动时间桶扫描。
    std::atomic<bool> recoveryInProgress_{false};
};
