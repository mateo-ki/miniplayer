#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QImage>
#include <QVector>
#include <QVariantList>
#include <functional>

#include "infrastructure/PoolApi.h"

class AuthController;

// 「我的」页聊天标签：pool 后端的群聊与私信。
// 鉴权同样复用 AuthController 的网页会话 Cookie，不另存一份凭据。
// pool 这边消息 id / 用户 id 全是字符串（消息是 UUID），所以在这层就不碰数字 id，
// 免得 QML 的 number 把长 id 变成 1.2e+18。
// 服务端硬限制（正文 ≤500 字、每人 2 秒一条、群聊留 7 天 / 私信留 1 天）在这里先挡一遍，
// 让用户立刻看到和后端一致的原因，而不是等一个 400。
class PoolChatController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString myUserId READ myUserId NOTIFY myUserIdChanged)
    Q_PROPERTY(int maxContentLength READ maxContentLength CONSTANT)

    Q_PROPERTY(QVariantList messages READ messages NOTIFY groupChanged)
    Q_PROPERTY(bool groupLoading READ groupLoading NOTIFY groupChanged)
    Q_PROPERTY(QString message READ groupMessage NOTIFY groupChanged)
    // 未读边界由服务端算：群聊看 chat_reads 游标，私信看 private_messages.read_at。
    // 交回的是消息 id 而不是时间串，视图按 id 划那条"以下为未读"的分隔线。
    Q_PROPERTY(QString groupFirstUnreadId READ groupFirstUnreadId NOTIFY groupChanged)

    // 红点那两个数只从 /api/chat/unread 来：列表回包里也带 unread，
    // 但两处都往界面喂就成了同一个概念两个数，所以那一路只取 first_unread_id。
    Q_PROPERTY(int unreadGroup READ unreadGroup NOTIFY unreadChanged)
    Q_PROPERTY(int unreadDm READ unreadDm NOTIFY unreadChanged)
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY unreadChanged)

    Q_PROPERTY(QVariantList users READ users NOTIFY usersChanged)

    Q_PROPERTY(QVariantList threads READ threads NOTIFY threadsChanged)
    Q_PROPERTY(bool threadsLoading READ threadsLoading NOTIFY threadsChanged)
    Q_PROPERTY(QString threadsMessage READ threadsMessage NOTIFY threadsChanged)

    Q_PROPERTY(QVariantList threadMessages READ threadMessages NOTIFY threadChanged)
    Q_PROPERTY(bool threadLoading READ threadLoading NOTIFY threadChanged)
    Q_PROPERTY(QString threadPeerName READ threadPeerName NOTIFY threadChanged)
    // 私信那条状态行读的字段：以前只有私有成员没有属性，
    // QML 里 poolChat.threadMessage 取到 undefined，写进 readonly string 就静默空一行。
    Q_PROPERTY(QString threadMessage READ threadMessage NOTIFY threadChanged)
    Q_PROPERTY(QString threadFirstUnreadId READ threadFirstUnreadId NOTIFY threadChanged)

    // ##片名## 语法的候选（和网页版同一个 /api/anime/list），以及待发的那张动漫卡片。
    Q_PROPERTY(QVariantList animeResults READ animeResults NOTIFY animeChanged)
    Q_PROPERTY(bool animeLoading READ animeLoading NOTIFY animeChanged)
    Q_PROPERTY(QString pendingAnimeName READ pendingAnimeName NOTIFY pendingAnimeChanged)
    Q_PROPERTY(QString pendingAnimePic READ pendingAnimePic NOTIFY pendingAnimeChanged)
    Q_PROPERTY(QString pendingAnimeId READ pendingAnimeId NOTIFY pendingAnimeChanged)

    // 发图：先传图床拿直链，挂在 pendingImages 上，发送时并进 messages/images 字段。
    Q_PROPERTY(QStringList pendingImages READ pendingImages NOTIFY pendingImagesChanged)
    Q_PROPERTY(bool imageUploading READ imageUploading NOTIFY pendingImagesChanged)
    Q_PROPERTY(QString imageMessage READ imageMessage NOTIFY pendingImagesChanged)
    // 可绑定的"剪贴板里有图"：QML 用它决定窗口级 Ctrl+V 要不要挂出来。
    // 没图的时候这个快捷键整个不存在，文本粘贴照旧走 TextInput。
    Q_PROPERTY(bool clipboardHasImage READ clipboardHasImage NOTIFY clipboardImageChanged)

public:
    static constexpr int kMaxContentLength = 500;
    static constexpr qint64 kMinSendIntervalMs = 2000;
    static constexpr int kPollIntervalMs = 5000;
    // 会话列表那条接口只有全量，一轮 50 行，所以它每 3 轮（15 秒）静默拉一次；
    // 正在看的对话走 after 游标，增量回包通常是空的，可以每轮都拉。
    static constexpr int kThreadsPollEveryTicks = 3;
    // 红点那条通道：30 秒一轮，请求体只有两个数字，跟在他当前视图里的轮询互不相干。
    static constexpr int kUnreadPollMs = 30000;
    static constexpr int kMaxKeptMessages = 400;
    // 和 chat.rs 对齐：一条最多 9 张、单张 5MB（超了服务端直接 400，这里先挡一遍）。
    static constexpr int kMaxImagesPerMessage = 9;
    static constexpr qint64 kMaxImageBytes = 5 * 1024 * 1024;

    explicit PoolChatController(AuthController *auth, QObject *parent = nullptr);

    bool active() const { return active_; }
    QString myUserId() const;
    int maxContentLength() const { return kMaxContentLength; }

    QVariantList messages() const { return messages_; }
    bool groupLoading() const { return groupLoading_; }
    // 单字段 multipart 体（name="file"）：纯函数，单测直接量它拼出来的字节。
    struct MultipartBody {
        QByteArray contentType;
        QByteArray body;
    };
    static MultipartBody buildImagePart(const QString &fileName, const QByteArray &bytes);
    // 一张待上传的图：来源可以是本地文件，也可以是剪贴板里的位图。
    struct ImageJob {
        QString name;
        QByteArray bytes;
    };
    // 剪贴板位图统一转 PNG —— 服务端只认 PNG/JPG/GIF/WEBP/BMP 的魔数。
    static QByteArray pngBytes(const QImage &image);
    static bool isImageFile(const QString &path);
    static QString localPathOf(const QUrl &url);
    void enqueuePaths(const QStringList &paths, int room);
    bool hasAttachment() const;
    void uploadNextQueuedImage();

    QString groupMessage() const { return groupMessage_; }
    QString groupFirstUnreadId() const { return groupFirstUnreadId_; }
    int unreadGroup() const { return unreadGroup_; }
    int unreadDm() const { return unreadDm_; }
    int unreadTotal() const { return unreadGroup_ + unreadDm_; }

    QVariantList users() const { return users_; }

    QVariantList threads() const { return threads_; }
    bool threadsLoading() const { return threadsLoading_; }
    QString threadsMessage() const { return threadsMessage_; }

    QVariantList animeResults() const { return animeResults_; }
    bool animeLoading() const { return animeLoading_; }
    QString pendingAnimeName() const { return pendingAnimeName_; }
    QString pendingAnimePic() const { return pendingAnimePic_; }
    QString pendingAnimeId() const { return pendingAnimeId_; }

    QStringList pendingImages() const { return pendingImages_; }
    bool imageUploading() const { return imageUploading_; }
    QString imageMessage() const { return imageMessage_; }

    QVariantList threadMessages() const { return threadMessages_; }
    bool threadLoading() const { return threadLoading_; }
    QString threadPeerName() const { return threadPeerName_; }
    QString threadMessage() const { return threadMessage_; }
    QString threadFirstUnreadId() const { return threadFirstUnreadId_; }

    // 聊天标签可见时打开轮询，离开就停：关掉后定时器不会再发一个请求。
    // 停下来的那一刻顺手把群聊进度交回服务端 —— 看到过才算读过。
    Q_INVOKABLE void setActive(bool active);

    /// 轮询只喂当前视图：0=群聊、1=私信会话列表、2=某个私信对话。
    /// 三个视图共用这一个定时器，切视图就换它拉的那条接口，不会并发竞速。
    Q_INVOKABLE void setViewMode(int mode);

    /// 红点的两个数：只拉 /api/chat/unread（服务端两条 COUNT，不传列表内容）。
    /// 没登录就一声不出（send() 那条通道本来就不发请求），30 秒一轮，
    /// 另外在"刚把群聊读过 / 刚打开一条私信会话"之后立刻补一次，让红点当场掉下去。
    Q_INVOKABLE void refreshUnread();

    /// 把群聊已读进度推到当前已经渲染出来的最后一条（服务端只准往前挪）。
    Q_INVOKABLE void markGroupRead();

    /// ##片名## 的候选列表：走服务端 /api/anime/list，与网页版同、同一套编号。
    Q_INVOKABLE void searchAnime(const QString &keyword);
    /// 选中一条 = 挂上待发送卡片；输入框里那段 ##…## 由视图负责抹掉。
    Q_INVOKABLE void pickAnime(const QString &name, const QString &pic, const QString &id);
    Q_INVOKABLE void clearAnime();

    /// 打开系统图片选择框（原生 Win32，不依赖 QML 对话框模块），选中的按顺序逐张上传。
    Q_INVOKABLE void chooseAndUploadImages();
    /// Ctrl+V：剪贴板里有图就排队上传；没有图就一声不出（把粘贴留给文本）。
    Q_INVOKABLE void pasteImageFromClipboard();
    /// 剪贴板里是不是一张图：QML 用它决定要不要吃掉 Ctrl+V；是文本就照常粘贴。
    Q_INVOKABLE bool clipboardHasImage() const;
    Q_INVOKABLE void removePendingImage(int index);
    Q_INVOKABLE void clearPendingImages();

    Q_INVOKABLE void refreshGroup();
    Q_INVOKABLE void sendGroupMessage(const QString &content);
    Q_INVOKABLE void deleteGroupMessage(const QString &messageId);
    Q_INVOKABLE void searchUsers(const QString &keyword);

    Q_INVOKABLE void refreshThreads();
    Q_INVOKABLE void openThread(const QString &peerId, const QString &peerName);
    Q_INVOKABLE void closeThread();
    Q_INVOKABLE void sendThreadMessage(const QString &content);
    Q_INVOKABLE void deleteThreadMessage(const QString &messageId);

    // UI 判定草稿能不能清：跟发送走同一个函数、同一个时钟，返回空串就是放行。
    Q_INVOKABLE QString groupBlockReason(const QString &content) const;
    Q_INVOKABLE QString threadBlockReason(const QString &content) const;

    // 纯决策逻辑：放行返回空串，否则返回要原样展示给用户的中文原因
    // （措辞跟服务端一致，避免同一件事两种说法）。
    // elapsedMs 是距上一条发出去的消息过了多久，从未发过时传一个大于限额的值。
    static QString sendBlockReason(const QString &content, qint64 elapsedMs);
    // 纯函数：一轮拉回来的条目就地接进已有列表 —— 只认没见过的 id（after 是 >= 比较，
    // 边界那条必然再回来一次），游标只往前推，超过上限从最老那头裁掉。
    // 返回"真的接进过东西"，调用方据此决定要不要发信号：列表没变就别重建 delegate。
    static bool appendUnseenStamps(QVariantList &list, QSet<QString> &seenIds, QString &cursor,
                                   const QList<QVariantMap> &stamps, qsizetype maxKept);
    /// 纯函数：把服务端的 {group,dm} 换成界面上该亮的两个数。
    /// 正在看群聊那一屏时群聊按已读算 —— 不然人盯着消息看，红点一直亮着。
    static QPair<int, int> unreadPairOf(const QJsonObject &data, bool viewingGroup);
    // 把正文里的 "@用户名" 命中到已缓存的候选人：先匹配长的，
    // 所以 @李明 不会再被算成 @李。返回服务端要的 [{"id":"..."}]。
    static QJsonArray mentionedUserIds(const QString &content, const QVariantList &users);

signals:
    void activeChanged();
    void pendingImagesChanged();
    void clipboardImageChanged();
    void animeChanged();
    void pendingAnimeChanged();
    void myUserIdChanged();
    void groupChanged();
    void usersChanged();
    void threadsChanged();
    void threadChanged();
    void unreadChanged();

private:
    using Callback = std::function<void(const PoolApi::Result &)>;

    void send(const QString &path, const QByteArray &body, const QByteArray &verb, Callback cb,
              const QByteArray &contentType = QByteArray(), int timeoutMs = 15000);
    void pollChatTick();
    void fetchGroup(const QString &after, bool showLoading);
    void appendGroupMessages(const QJsonArray &items);
    void fetchThread(const QString &after, bool showLoading);
    // 就地追加：只接没见过的那几条，返回真的接上了才让调用方发信号（列表没变就别重建 delegate）。
    bool appendThreadMessages(const QJsonArray &items);
    void pollThreadsQuietly();
    QByteArray groupBodyFor(const QString &content) const;
    // 待发动漫卡片算"有内容"：只有空草稿这一条因它放行，长度和 2 秒限速照旧。
    static QString blockReasonFor(const QString &content, qint64 elapsedMs, bool hasAttachment);
    QJsonObject pendingAnimeBody() const;
    void clearPendingAnime();
    QVariantMap toStamp(QJsonValue item) const;
    void removeMessageById(QVariantList &list, const QString &messageId) const;
    qint64 elapsedSince(const QElapsedTimer &timer) const;

    AuthController *auth_ = nullptr;
    QNetworkAccessManager nam_;
    QTimer pollTimer_;
    QTimer unreadTimer_;

    bool active_ = false;
    int viewMode_ = 0;
    int tickCount_ = 0;
    QString groupCursor_;
    QSet<QString> seenGroupIds_;
    QVariantList messages_;
    bool groupLoading_ = false;
    bool groupFetchInFlight_ = false;
    bool groupFullRefreshQueued_ = false;
    QString groupMessage_;
    QString groupFirstUnreadId_;
    int unreadGroup_ = 0;
    int unreadDm_ = 0;
    bool unreadInFlight_ = false;
    bool unreadWarned_ = false;
    bool unreadLoggedOnce_ = false;
    QElapsedTimer groupSendClock_;

    QVariantList users_;

    QVariantList threads_;
    bool threadsLoading_ = false;
    bool threadsPollInFlight_ = false;
    QString threadsMessage_;

    QString threadPeerId_;
    QString threadPeerName_;
    QVariantList threadMessages_;
    bool threadLoading_ = false;
    bool threadFetchInFlight_ = false;
    QString threadCursor_;
    QSet<QString> seenThreadIds_;
    QString threadMessage_;
    QString threadFirstUnreadId_;
    QElapsedTimer dmSendClock_;

    QVariantList animeResults_;
    bool animeLoading_ = false;
    quint64 animeSerial_ = 0;
    QString pendingAnimeName_;
    QString pendingAnimePic_;
    QString pendingAnimeId_;

    QStringList pendingImages_;
    QVector<ImageJob> imageQueue_;
    int imageQueueTotal_ = 0;
    bool imageUploading_ = false;
    QString imageMessage_;
    bool clipboardHasImage_ = false;
};
