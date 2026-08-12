#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QUrl>
#include <QVector>

struct ApiSite {
    QString name;
    QString baseUrl;
    QString type = QStringLiteral("video");
};

class ApiSiteModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentSiteChanged)
    Q_PROPERTY(QString currentBaseUrl READ currentBaseUrl NOTIFY currentSiteChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        BaseUrlRole,
        SiteTypeRole,
        ShareSelectedRole,
        AccessStatusRole,
        AccessStatusTextRole,
        AccessLatencyMsRole
    };

    explicit ApiSiteModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);

    Q_INVOKABLE void add(const QString &name, const QString &baseUrl, const QString &siteType = QStringLiteral("video"));
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void update(int index, const QString &name, const QString &baseUrl, const QString &siteType = QStringLiteral("video"));
    Q_INVOKABLE bool selectAt(int index);
    Q_INVOKABLE QString nameAt(int index) const;
    Q_INVOKABLE QString baseUrlAt(int index) const;
    Q_INVOKABLE QString typeAt(int index) const;
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
    Q_INVOKABLE QString shareSelectedToClipboard();
    Q_INVOKABLE QString importSitesFromClipboard();
    Q_INVOKABLE bool hasShareContentInClipboard() const;
    Q_INVOKABLE void refreshSiteStatusAt(int index);
    Q_INVOKABLE void refreshAllSiteStatuses();

signals:
    void countChanged();
    void currentIndexChanged();
    void currentSiteChanged();

private:
    QVector<ApiSite> items_;
    QVector<bool> shareSelected_;
    QVector<int> accessStatus_;
    QVector<int> accessLatencyMs_;
    QNetworkAccessManager accessManager_;
    int currentIndex_ = 0;
    QString configPath_;

    void loadFromFile();
    void saveToFile();
    void ensureDefaults();
    void ensureShareSelectionSize();
    void ensureAccessStateSize();
    void setAccessState(int index, int status, int latencyMs = -1);

    static QString resolveConfigPath();
    static QString normalizeBaseUrl(const QString &baseUrl);
    static QString normalizeSiteType(const QString &siteType);
    static QString accessStatusText(int status, int latencyMs);
    static QUrl statusCheckUrl(const QString &baseUrl);
    static QString encodeSharePayload(const QJsonObject &payload);
    static bool decodeSharePayload(const QString &text, QJsonObject *payload);
};
