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
 * Dmghg (动漫共和国) 直连接口客户端。
 *
 * 协议移植自 D:\\project\\ida\\dm 下的 dmghg_api.py / jx_parse.py:
 *  - 主网关请求带 AES-256-CBC 鉴权头 + RSA-2048 解密响应。
 *  - 搜索走独立网关,带 md5 sign + t 参数。
 *  - play 接口返回的 source 标识需要二次解析(new.jx.dokiapp.tech),
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
    void search(const QString &keyword, int page, int limit,
                const std::function<void(bool, const QVariantList &, const QString &)> &callback);

    // 视频列表。channel=0 默认。
    void listVideos(int channel, int page, int limit, const QString &sort, const QString &type,
                    const std::function<void(bool, const QVariantList &, int total, const QString &)> &callback);

    // 视频详情,返回元信息 + parts 分集列表。
    void videoDetail(int vid,
                     const std::function<void(bool, const QVariantMap &, const QString &)> &callback);

    // 播放列表,返回 play 接口的 data[0].url 源标识。
    void playVideo(int vid, const QString &part, const QString &playSource,
                   const std::function<void(bool, const QString &source, const QString &)> &callback);

    // 二次解析: source 标识 -> 真实流地址列表。每条含 url/type/name。
    void parseSource(const QString &source,
                     const std::function<void(bool, const QVariantList &items, const QString &)> &callback);

    void comments(int vid, int page, int limit,
                  const std::function<void(bool, const QVariantList &items, int total, const QString &)> &callback);
    void createComment(const QString &comment, const QString &uuid, const QString &dots,
                       const std::function<void(bool, const QString &)> &callback);

    // 站内弹幕(按毫秒时间窗口)。移植自 dmghg_api.py::get_danmaku_internal。
    // vid 视频ID, part 集名, play 播放源(空则"cn"), startMs/endMs 窗口起止(毫秒)。
    // 回调返回 (ok, items[{id,text,color,timePoint,mode,size}], total, err)。
    // items[].content 是 JSON 字符串,这里二次 parse 出 color/content/size/type。
    void danmaku(int vid, const QString &part, const QString &play, int startMs, int endMs,
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

    // 后台时间桶恢复:派生 IV 被拒 (403501/30000) 时触发,
    // 螺旋扫描邻近 10000s 时间桶, 命中即写入 gLiveBucketOffset 修正本机时钟漂移。
    // 异步、不阻塞当前请求;恢复后下一次请求自动命中。
    void recoverSessionIv();

    // 解析失败响应体:明文 JSON 直接返回,密文走 RSA+AES。
    static QByteArray decryptBody(const QByteArray &body);

    // jx 解析链路内部方法
    static QByteArray aes128CbcDecrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &base64Data);
    static QByteArray aes128CbcEncrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &plain);

    QPointer<QNetworkReply> primaryReply_; // 取消用(单活跃主请求)

    // 后台恢复并发护栏:同一时刻只允许一个 recoverSessionIv 在跑,
    // 防止多个失败请求各自启动时间桶扫描。
    std::atomic<bool> recoveryInProgress_{false};
};
