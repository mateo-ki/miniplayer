#pragma once

#include <QHash>
#include <QString>
#include <QVariantList>
#include <functional>

#include "media/DmghgClient.h"
#include "MagicCrypto.h"

/**
 * *** v1.0.8+ 新协议客户端（/api/v1/app/client/*）。
 *
 * 继承旧 DmghgClient 并以相同签名覆盖全部业务方法 —— 上层（DmghgAnimeModel）
 * 通过 dmghg::useV3() 二选一，其余代码零改动。jx 二次解析层新旧共用，直接复用基类实现。
 *
 * id 的处理：新版视频 id 是 19 位雪花号，放不进 int（旧接口全是 int vid）。
 * 因此本类维护一张会话级句柄表：列表/搜索结果里每个新 id 分配一个 int 句柄
 * （从 1e9 起，避免与真实旧 id 撞号），对外仍以 int vodId 流转；
 * 收到的 int 若不在表里且是短数字，按旧 id 走 video/idMapping 换取新 id。
 * 切换协议或重启进程后句柄失效，由上层重新拉列表自然重建。
 */
class DmghgClientV3 final : public DmghgClient {
    Q_OBJECT
public:
    explicit DmghgClientV3(QObject *parent = nullptr);

    void search(const QString &keyword, int page, int limit,
                const std::function<void(bool, const QVariantList &, const QString &)> &callback) override;
    void listVideos(int channel, int page, int limit, const QString &sort, const QString &type,
                    const std::function<void(bool, const QVariantList &, int total, const QString &)> &callback) override;
    void videoDetail(int vid,
                     const std::function<void(bool, const QVariantMap &, const QString &)> &callback) override;
    // 18 位新 id 直接登记进句柄表；旧的短整数留给 video/idMapping 换。
    int handleForKey(const QString &key) override;
    // 句柄 -> 新 id 的反查（不在表里返回空串）。单测用它验 intern 是否可重入。
    QString newIdForHandle(int handle) const { return newIdByHandle_.value(handle); }
    void playVideo(int vid, const QString &part, const QString &playSource,
                   const std::function<void(bool, const QString &, const QString &)> &callback) override;
    void parseSource(const QString &source,
                     const std::function<void(bool, const QVariantList &, const QString &)> &callback) override;
    void comments(int vid, int page, int limit,
                  const std::function<void(bool, const QVariantList &, int, const QString &)> &callback) override;
    void createComment(const QString &comment, const QString &uuid, const QString &dots,
                       const std::function<void(bool, const QString &)> &callback) override;
    void danmaku(int vid, const QString &part, const QString &play, int startMs, int endMs,
                 const std::function<void(bool, const QVariantList &, int, const QString &)> &callback) override;
    // 频道 = type/listActive 的顶层分类；类型 = filter/dimensions 的 category 维度选项。
    void loadChannelOptions(const FilterCb &callback) override;
    void loadTypeOptions(const FilterCb &callback) override;
    void loadWeeklySchedule(
        const std::function<void(bool, const QVariantMap &days, const QString &err)> &callback) override;

    // 会话准备（幂等）；deviceId 首启生成并持久化，与真实设备隔离风控。
    void ensureSession(const std::function<void(bool, const QString &)> &done = {});
    static QString deviceId();

private:
    using JsonCb = std::function<void(bool, const QJsonObject &, const QString &)>;

    void post(const QString &path, const QJsonObject &body, const JsonCb &cb);
    void postAttempt(const QString &path, const QJsonObject &body, const JsonCb &cb, int attempt);
    void loadBootstrap(std::function<void(bool, const QString &)> done);
    // int vid -> 新雪花 id：句柄表直查，未知的短数字走 video/idMapping。
    void toNewId(int vid, const std::function<void(bool, const QString &, const QString &)> &cb);
    int intern(const QString &newId);
    // 并发给每个播放源取分集（各源互不依赖），全部回齐后才回调上层。
    void fillEpisodes(const QString &vid, const QVariantList &playSources,
                      const std::function<void(bool, const QVariantMap &, const QString &)> &cb,
                      const QVariantMap &base);

    QNetworkAccessManager nam_;
    dmghg::BootstrapConfig cfg_;
    QByteArray secret_;
    bool bootstrapping_ = false;
    QVector<std::function<void(bool, const QString &)>> waiters_;

    QHash<QString, int> handleByNewId_;
    QHash<int, QString> newIdByHandle_;
    int nextHandle_ = 1000000000;

    // 详情回填的播放源: name(playerCode) -> 供 playVideo 还原; 集名列表供集名->集号。
    QString currentVid_;
    int currentEpisode_ = 1;
    QString currentCode_ = QStringLiteral("cn");
    QHash<QString, QString> codeByName_;          // 本次详情的播放源
    QHash<QString, QVariantList> episodeNames_;   // "newId|code" -> 集名
    QVariantList categories_;                     // [{id,name}] 顶层频道，懒加载
    QString categoriesError_;
    bool categoriesLoading_ = false;
    QVector<std::function<void()>> categoryWaiters_;
    // filter/dimensions 的 category 维度：类型名下标 -> optionId。
    QVariantList typeTags_;
    QHash<QString, QString> typeTagIds_;
    QString typeTagsError_;
    bool typeTagsLoading_ = false;
    QVector<std::function<void()>> typeTagWaiters_;
    QVariantMap cachedSchedule_;
    qint64 scheduleCachedAtMs_ = 0;
    bool scheduleLoading_ = false;
    QVector<std::function<void(bool, const QVariantMap &, const QString &)>> scheduleWaiters_;
    void loadCategories(const std::function<void()> &done);
    void loadTypeTags(const std::function<void()> &done);
};
