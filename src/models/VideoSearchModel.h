#pragma once

#include <QAbstractListModel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QVariantList>
#include <QString>
#include <QVector>

struct VodItem {
    int vodId = 0;
    QString vodName;
    QString vodPic;
    QString vodRemarks;
    QString vodYear;
    QString vodArea;
    QString vodClass;
    QString vodActor;
    QString vodDirector;
    QString vodBlurb;
    QString vodContent;
    QString vodPlayFrom;  // 线路名，$$$ 分隔
    QString vodPlayUrl;   // 播放链接，$$$ 分隔
    QString vodScore;
    QString typeName;
};

class VideoSearchModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int totalPages READ totalPages NOTIFY totalPagesChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY loadingMoreChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY totalPagesChanged)
    Q_PROPERTY(bool restoredFromCache READ restoredFromCache NOTIFY restoredFromCacheChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum Roles {
        VodIdRole = Qt::UserRole + 1,
        VodNameRole,
        VodPicRole,
        VodRemarksRole,
        VodYearRole,
        VodAreaRole,
        VodClassRole,
        VodActorRole,
        VodDirectorRole,
        VodBlurbRole,
        VodContentRole,
        VodPlayFromRole,
        VodPlayUrlRole,
        VodScoreRole,
        TypeNameRole
    };

    explicit VideoSearchModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool loading() const;
    int totalCount() const;
    int count() const;
    int currentPage() const;
    int totalPages() const;
    bool loadingMore() const;
    bool hasMore() const;
    bool restoredFromCache() const;
    QVariantList categories() const;
    QString errorMessage() const;

    Q_INVOKABLE void search(const QString &baseUrl, const QString &keyword, int page = 1,
                            bool forceRefresh = false, bool append = false);
    Q_INVOKABLE void searchById(const QString &baseUrl, const QString &ids);
    Q_INVOKABLE void loadList(const QString &baseUrl, int page = 1, const QString &typeId = {},
                              bool forceRefresh = false, bool append = false);
    Q_INVOKABLE void clear();
    Q_INVOKABLE VodItem itemAt(int index) const;
    Q_INVOKABLE QVariantMap itemMapAt(int index) const;

signals:
    void loadingChanged();
    void totalCountChanged();
    void countChanged();
    void currentPageChanged();
    void totalPagesChanged();
    void loadingMoreChanged();
    void restoredFromCacheChanged();
    void categoriesChanged();
    void errorMessageChanged();
    void searchCompleted();
    void detailReceived(int index);

private:
    QVector<VodItem> items_;
    QNetworkAccessManager nam_;
    bool loading_ = false;
    int totalCount_ = 0;
    int currentPage_ = 1;
    int totalPages_ = 1;
    bool loadingMore_ = false;
    bool restoredFromCache_ = false;
    int requestSerial_ = 0;
    QVariantList categories_;
    QString errorMessage_;

    void setLoading(bool loading);
    void setErrorMessage(const QString &msg);
    void parseListResponse(const QJsonDocument &doc, const QString &baseUrl,
                           bool append = false, bool requestSupplement = true);
    void parseDetailResponse(const QJsonDocument &doc, const QString &baseUrl);
    void requestSupplementDetails(const QString &baseUrl, int requestSerial);
    QNetworkRequest makeRequest(const QString &url) const;
    QString cacheKey(const QString &type, const QString &baseUrl,
                     const QString &parameter, int page) const;
    bool restoreCache(const QString &key, bool append);
    void writeCache(const QString &key, const QJsonDocument &doc, const QString &baseUrl);
    void pruneCaches();
    void setLoadingMore(bool loading);
    void setRestoredFromCache(bool restored);
};
