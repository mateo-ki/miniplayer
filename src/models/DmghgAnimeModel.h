#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QDateTime>

class DmghgClient;

/**
 * 动漫共和国(Dmghg)专用搜索/列表/详情模型。
 *
 * 与通用 VideoSearchModel 不同,该接口走加密直连网关,不能复用 CMS 风格
 * 的 ?ac=detail 查询,因此独立建模。模型只承载搜索/列表结果;详情与选集
 * 通过详情信号单独下发,避免把分集列表塞进列表角色。
 */
class DmghgAnimeModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int totalPages READ totalPages NOTIFY totalPagesChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY detailLoadingChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantList episodes READ episodes NOTIFY detailChanged)
    Q_PROPERTY(int currentSource READ currentSource WRITE setCurrentSource NOTIFY detailChanged)
    Q_PROPERTY(bool commentsLoading READ commentsLoading NOTIFY commentsChanged)
    Q_PROPERTY(QVariantList comments READ comments NOTIFY commentsChanged)
    Q_PROPERTY(QVariantList danmaku READ danmaku NOTIFY danmakuChanged)

public:
    enum Roles {
        VodIdRole = Qt::UserRole + 1,
        VodNameRole,
        VodPicRole,
        VodRemarksRole,
        VodYearRole,
        VodAreaRole,
        TypeNameRole
    };

    explicit DmghgAnimeModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool loading() const;
    int totalCount() const;
    int count() const;
    int currentPage() const;
    int totalPages() const;
    QString errorMessage() const;
    bool detailLoading() const;
    QVariantMap detail() const;
    QVariantList episodes() const;
    int currentSource() const;
    void setCurrentSource(int source);

    Q_INVOKABLE void search(const QString &keyword, int page = 1);
    Q_INVOKABLE void loadList(int page = 1, const QString &type = QString(), int channel = 0);
    Q_INVOKABLE void loadDetail(int vodId);
    /// 请求播放某一集:vid + 集名。完成后发出 episodeResolved。
    Q_INVOKABLE void playEpisode(int vid, const QString &part);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void loadComments(int vid, int page = 1);
    Q_INVOKABLE void submitComment(const QString &text);
    bool commentsLoading() const;
    QVariantList comments() const;
    /// 拉取站内弹幕(分集):60s 窗口串行翻页,按 id 去重,完成后逐窗 emit danmakuChanged。
    Q_INVOKABLE void loadDanmaku(int vid, const QString &part);
    QVariantList danmaku() const;

signals:
    void loadingChanged();
    void totalCountChanged();
    void countChanged();
    void currentPageChanged();
    void totalPagesChanged();
    void errorMessageChanged();
    void detailLoadingChanged();
    void detailChanged();
    /// 真实流地址解析完成。url 为空表示失败,message 为提示。
    void episodeResolved(const QString &url, const QString &title, const QString &message);
    void commentsChanged();
    void danmakuChanged();

private:
    struct Item {
        int vodId = 0;
        QString vodName;
        QString vodPic;
        QString vodRemarks;
        QString vodYear;
        QString vodArea;
        QString typeName;
    };

    DmghgClient *client();
    void setLoading(bool loading);
    void setDetailLoading(bool loading);
    void setErrorMessage(const QString &msg);
    void publishSearchResults(const QVariantList &items);
    void publishDetail(const QVariantMap &detail);
    // 串行拉取下一个 60s 弹幕窗口;内部递归,受 serial 失效保护。
    void fetchDanmakuWindow(quint64 serial, int vid, const QString &part, const QString &play);

    struct CachedPage {
        QVariantList items;
        int total = 0;
        qint64 expiresAt = 0;
    };
    bool loadCachedPage(const QString &key, quint64 serial, bool hasTotal);
    void saveCachedPage(const QString &key, const QVariantList &items, int total);

    QVector<Item> items_;
    bool loading_ = false;
    bool detailLoading_ = false;
    int totalCount_ = 0;
    int currentPage_ = 1;
    int totalPages_ = 1;
    QString errorMessage_;
    QVariantMap detail_;
    QVariantList episodes_;          // 当前线路的选集名称列表
    QVariantList playSources_;       // 所有线路 [{name, episodes:[...]}]
    int currentSource_ = 0;
    quint64 requestSerial_ = 0;
    QHash<QString, CachedPage> pageCache_;
    bool commentsLoading_ = false;
    QVariantList comments_;
    int commentsVid_ = 0;

    // 站内弹幕(按毫秒时间分页拉取,按 id 去重)。
    QVariantList danmaku_;
    QHash<qint64, qint64> danmakuSeen_;   // id -> 1 (去重)
    qint64 danmakuCursorMs_ = 0;          // 下一个 60s 窗口起点
    int danmakuWindows_ = 0;              // 已翻页数(封顶 max_windows)
    quint64 danmakuSerial_ = 0;           // 弹幕请求序号(独立于 requestSerial_,
                                          //  以免弹幕加载使选集解析回调失效)
    static constexpr int kDanmakuMaxWindows = 240; // 4h 封顶
};
