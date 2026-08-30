#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>

class BeeClient;

class BeeVideoModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY detailLoadingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(bool hasNextPage READ hasNextPage NOTIFY paginationChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantList episodes READ episodes NOTIFY detailChanged)
public:
    enum Roles {
        VodIdRole = Qt::UserRole + 1,
        VodNameRole,
        VodPicRole,
        VodRemarksRole,
        TypeNameRole,
        VodYearRole,
        VodAreaRole,
        VodClassRole,
        VodBlurbRole,
        VodVersionRole
    };
    explicit BeeVideoModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool loading() const;
    bool detailLoading() const;
    int count() const;
    int currentPage() const;
    bool hasNextPage() const;
    QString errorMessage() const;
    QVariantMap detail() const;
    QVariantList episodes() const;
    Q_INVOKABLE void loadRecommended(int page = 1);
    Q_INVOKABLE void search(const QString &keyword);
    Q_INVOKABLE void loadDetail(const QString &vodId);
    Q_INVOKABLE void clear();
signals:
    void loadingChanged();
    void detailLoadingChanged();
    void countChanged();
    void currentPageChanged();
    void paginationChanged();
    void errorMessageChanged();
    void detailChanged();
    void episodeRequested(const QString &url, const QString &title);
private:
    struct Item {
        QString id;
        QString name;
        QString pic;
        QString remarks;
        QString type;
        QString year;
        QString area;
        QString vodClass;
        QString blurb;
        QString version;
    };
    BeeClient *client();
    void replaceItems(const QVariantList &items);
    void fetchItemImages(int requestSerial);
    void fetchItemImage(int requestSerial, int index);
    QVariantMap itemMap(const Item &item) const;
    void setLoading(bool value);
    void setDetailLoading(bool value);
    void setError(const QString &value);
    void setPagination(int page, bool hasNext);
    QVector<Item> items_;
    QVariantMap detail_;
    QVariantList episodes_;
    bool loading_ = false;
    bool detailLoading_ = false;
    QString error_;
    int listRequestSerial_ = 0;
    int currentPage_ = 1;
    bool hasNextPage_ = false;
    QString detailRequestId_;
};
