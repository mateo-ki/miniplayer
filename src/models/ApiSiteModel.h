#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVector>

class QNetworkReply;

struct ApiSite {
    QString name;
    QString baseUrl;
    QString type = QStringLiteral("video");
    bool premium = false;
};

class ApiSiteModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentSiteChanged)
    Q_PROPERTY(QString currentBaseUrl READ currentBaseUrl NOTIFY currentSiteChanged)
    Q_PROPERTY(bool remoteSitesLoading READ remoteSitesLoading NOTIFY remoteSitesLoadingChanged)
    Q_PROPERTY(bool jsonSitesLoading READ jsonSitesLoading NOTIFY jsonSitesLoadingChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        BaseUrlRole,
        SiteTypeRole,
        ShareSelectedRole,
        AccessStatusRole,
        AccessStatusTextRole,
        AccessLatencyMsRole,
        PremiumRole
    };

    explicit ApiSiteModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    bool remoteSitesLoading() const;
    bool jsonSitesLoading() const;

    Q_INVOKABLE void add(const QString &name, const QString &baseUrl, const QString &siteType = QStringLiteral("video"), bool premium = false);
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void update(int index, const QString &name, const QString &baseUrl, const QString &siteType = QStringLiteral("video"));
    Q_INVOKABLE bool moveSite(int from, int to);
    Q_INVOKABLE bool moveSiteToSlot(int from, int slot);
    Q_INVOKABLE bool selectAt(int index);
    Q_INVOKABLE QString nameAt(int index) const;
    Q_INVOKABLE QString baseUrlAt(int index) const;
    Q_INVOKABLE QString typeAt(int index) const;
    Q_INVOKABLE bool premiumAt(int index) const;
    Q_INVOKABLE void setPremium(int index, bool premium);
    Q_INVOKABLE void togglePremium(int index);
    Q_INVOKABLE bool shareSelectedAt(int index) const;
    Q_INVOKABLE int accessStatusAt(int index) const;
    Q_INVOKABLE QString accessStatusTextAt(int index) const;
    Q_INVOKABLE QString currentBaseUrl() const;
    Q_INVOKABLE QString currentVideoBaseUrl() const;
    Q_INVOKABLE QString imageBaseUrl() const;
    Q_INVOKABLE QString currentName() const;
    Q_INVOKABLE bool matchesFilter(int index, const QString &filter) const;
    Q_INVOKABLE QString deduplicateByUrl();
    Q_INVOKABLE void setShareSelected(int index, bool selected);
    Q_INVOKABLE void toggleShareSelected(int index);
    Q_INVOKABLE void selectAllForShare(bool selected);
    Q_INVOKABLE QString removeSelectedSites();
    Q_INVOKABLE QString shareSelectedToClipboard();
    Q_INVOKABLE QString importSitesFromClipboard();
    Q_INVOKABLE bool hasShareContentInClipboard() const;
    Q_INVOKABLE void loadRemoteSites();
    Q_INVOKABLE void loadJsonVideoSites();
    QString importJsonVideoSites(const QByteArray &content, bool refreshStatuses = true);
    Q_INVOKABLE void refreshSiteStatusAt(int index);
    Q_INVOKABLE void refreshAllSiteStatuses();

signals:
    void countChanged();
    void currentIndexChanged();
    void currentSiteChanged();
    void siteContentChanged();
    void orderChanged();
    void remoteSitesLoadingChanged();
    void remoteSitesLoadFinished(const QString &message);
    void jsonSitesLoadingChanged();
    void jsonSitesLoadFinished(const QString &message);

private:
    QVector<ApiSite> items_;
    QVector<bool> shareSelected_;
    QVector<int> accessStatus_;
    QVector<int> accessLatencyMs_;
    QNetworkAccessManager accessManager_;
    QPointer<QNetworkReply> remoteSitesReply_;
    QPointer<QNetworkReply> jsonSitesReply_;
    QQueue<QString> pendingStatusChecks_;
    QSet<QString> activeStatusChecks_;
    int currentIndex_ = 0;
    QString configPath_;
    bool remoteSitesLoading_ = false;
    bool jsonSitesLoading_ = false;

    void loadFromFile();
    void saveToFile();
    void ensureDefaults();
    void ensureShareSelectionSize();
    void ensureAccessStateSize();
    void setAccessState(int index, int status, int latencyMs = -1);
    void setRemoteSitesLoading(bool loading);
    void setJsonSitesLoading(bool loading);
    QString importSitesFromText(const QString &text);
    void enqueueSiteStatusChecks(const QVector<QString> &normalizedUrls);
    void pumpSiteStatusChecks();
    void finishSiteStatusCheck(const QString &normalizedUrl);
    bool enforcePremiumOrder();
    int premiumSiteCount() const;

    static QString resolveConfigPath();
    static QString normalizeBaseUrl(const QString &baseUrl);
    static QString normalizeSiteType(const QString &siteType);
    static QString accessStatusText(int status, int latencyMs);
    static QUrl statusCheckUrl(const QString &baseUrl);
    static QString encodeSharePayload(const QJsonObject &payload);
    static bool decodeSharePayload(const QString &text, QJsonObject *payload);
};
