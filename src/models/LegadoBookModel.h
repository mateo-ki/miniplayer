#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include "book/BookSource.h"
#include "book/LegadoClient.h"
#include "book/RuleEngine.h"

class LegadoSourceModel;

// 基于当前选中书源的书籍流程：搜索/发现 → 详情 → 目录 → 正文；
// 音频源（bookSourceType == 1）在解析章节地址后发出 playbackRequested 交给播放器。
class LegadoBookModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY detailLoadingChanged)
    Q_PROPERTY(bool tocLoading READ tocLoading NOTIFY tocLoadingChanged)
    Q_PROPERTY(bool contentLoading READ contentLoading NOTIFY contentLoadingChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY pageChanged)
    Q_PROPERTY(bool hasNextPage READ hasNextPage NOTIFY pageChanged)
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY sourceChanged)
    Q_PROPERTY(bool isAudioSource READ isAudioSource NOTIFY sourceChanged)
    Q_PROPERTY(bool isImageSource READ isImageSource NOTIFY sourceChanged)
    Q_PROPERTY(bool hasExplore READ hasExplore NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList exploreEntries READ exploreEntries NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY tocChanged)
    Q_PROPERTY(int chapterCount READ chapterCount NOTIFY tocChanged)
    Q_PROPERTY(QString content READ content NOTIFY contentChanged)
    Q_PROPERTY(QString contentChapterTitle READ contentChapterTitle NOTIFY contentChanged)
    Q_PROPERTY(int contentChapterIndex READ contentChapterIndex NOTIFY contentChanged)
    Q_PROPERTY(bool hasDetail READ hasDetail NOTIFY detailChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        AuthorRole,
        CoverRole,
        IntroRole,
        KindRole,
        LastChapterRole,
        BookUrlRole
    };

    explicit LegadoBookModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool loading() const;
    bool detailLoading() const;
    bool tocLoading() const;
    bool contentLoading() const;
    QString message() const;
    int count() const;
    int currentPage() const;
    bool hasNextPage() const;
    bool hasSource() const;
    bool isAudioSource() const;
    bool isImageSource() const;
    bool hasExplore() const;
    QVariantList exploreEntries() const;
    QString sourceName() const;
    QVariantMap detail() const;
    QVariantList chapters() const;
    int chapterCount() const;
    QString content() const;
    QString contentChapterTitle() const;
    int contentChapterIndex() const;
    bool hasDetail() const;

    Q_INVOKABLE void search(const QString &keyword);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void loadExploreEntry(int index);
    Q_INVOKABLE void loadDetail(int row);
    Q_INVOKABLE void loadToc();
    Q_INVOKABLE void loadContent(int chapterIndex);
    Q_INVOKABLE void playChapter(int chapterIndex);
    Q_INVOKABLE void clearResults();

    void setSourceModel(LegadoSourceModel *sourceModel);
    void applyActiveSource();

signals:
    void loadingChanged();
    void detailLoadingChanged();
    void tocLoadingChanged();
    void contentLoadingChanged();
    void messageChanged();
    void countChanged();
    void pageChanged();
    void sourceChanged();
    void detailChanged();
    void tocChanged();
    void contentChanged();
    void playbackRequested(const QString &url, const QString &title);

private:
    struct SearchItem {
        QString name;
        QString author;
        QString coverUrl;
        QString bookUrl;
        QString intro;
        QString kind;
        QString lastChapter;
    };
    struct ChapterItem {
        QString title;
        QString url;
        bool isVolume = false;
    };

    bool ensureSourceReady();
    RuleContext makeContext(const QString &baseUrl, const QString &key, int page);
    bool doFetch(const QString &urlRule, const QString &key, int page,
                 const QString &stageName, RuleContext *ctx, LegadoFetchResult *result);
    void fillBooksFromRules(const RuleContext &ctx, const QVariantMap &rules,
                            bool append);
    void runDetail(const QString &bookUrl);
    void runToc();
    void runContent(int chapterIndex, bool forPlayback);
    void setLoading(bool value);
    void setDetailLoading(bool value);
    void setTocLoading(bool value);
    void setContentLoading(bool value);
    void setMessage(const QString &message);
    static QString absolutize(const QString &value, const QString &baseUrl);
    void rebuildExploreEntries();

    LegadoSourceModel *sourceModel_ = nullptr;
    BookSource currentSource_;
    bool hasSource_ = false;
    LegadoClient client_;
    RuleEngine *engine_ = nullptr;

    QVector<SearchItem> items_;
    QVariantMap detail_;
    bool hasDetail_ = false;
    QVector<ChapterItem> chapters_;
    QString content_;
    QString contentChapterTitle_;
    int contentChapterIndex_ = -1;
    QVariantList exploreEntries_;
    QString keyword_;
    int currentPage_ = 1;
    bool hasNextPage_ = false;
    bool loading_ = false;
    bool detailLoading_ = false;
    bool tocLoading_ = false;
    bool contentLoading_ = false;
    QString message_;
    quint64 generation_ = 0;  // 源切换/清空时递增，使在途流程的结果作废
};
