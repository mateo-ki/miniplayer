#include "BeeVideoModel.h"
#include "media/BeeClient.h"

#include <QCoreApplication>
#include <QtGlobal>

#include <utility>

namespace {
// 近期热播 (mb) returns 10 cards per page; the server reports total_pages so
// this is only used as a sanity bound on page fill for the legacy slice path.
constexpr int kRecommendedPageSize = 10;
}

BeeVideoModel::BeeVideoModel(QObject *parent)
    : QAbstractListModel(parent) {}

int BeeVideoModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : items_.size();
}

QVariant BeeVideoModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size())
        return {};
    const Item &item = items_.at(index.row());
    switch (role) {
    case VodIdRole: return item.id;
    case VodNameRole: return item.name;
    case VodPicRole: return item.pic;
    case VodRemarksRole: return item.remarks;
    case TypeNameRole: return item.type;
    case VodYearRole: return item.year;
    case VodAreaRole: return item.area;
    case VodClassRole: return item.vodClass;
    case VodBlurbRole: return item.blurb;
    case VodVersionRole: return item.version;
    default: return {};
    }
}

QHash<int, QByteArray> BeeVideoModel::roleNames() const {
    return {
        {VodIdRole, "vodId"},
        {VodNameRole, "vodName"},
        {VodPicRole, "vodPic"},
        {VodRemarksRole, "vodRemarks"},
        {TypeNameRole, "typeName"},
        {VodYearRole, "vodYear"},
        {VodAreaRole, "vodArea"},
        {VodClassRole, "vodClass"},
        {VodBlurbRole, "vodBlurb"},
        {VodVersionRole, "vodVersion"}
    };
}

bool BeeVideoModel::loading() const { return loading_; }
bool BeeVideoModel::detailLoading() const { return detailLoading_; }
int BeeVideoModel::count() const { return items_.size(); }
int BeeVideoModel::currentPage() const { return currentPage_; }
bool BeeVideoModel::hasNextPage() const { return hasNextPage_; }
QString BeeVideoModel::errorMessage() const { return error_; }
QVariantMap BeeVideoModel::detail() const { return detail_; }
QVariantList BeeVideoModel::episodes() const { return episodes_; }

void BeeVideoModel::setLoading(bool value) {
    if (loading_ == value) return;
    loading_ = value;
    emit loadingChanged();
}

void BeeVideoModel::setDetailLoading(bool value) {
    if (detailLoading_ == value) return;
    detailLoading_ = value;
    emit detailLoadingChanged();
}

void BeeVideoModel::setError(const QString &value) {
    if (error_ == value) return;
    error_ = value;
    emit errorMessageChanged();
}

void BeeVideoModel::setPagination(int page, bool hasNext) {
    if (currentPage_ != page) {
        currentPage_ = page;
        emit currentPageChanged();
    }
    if (hasNextPage_ != hasNext) {
        hasNextPage_ = hasNext;
        emit paginationChanged();
    }
}

BeeClient *BeeVideoModel::client() {
    static BeeClient *instance = new BeeClient(qApp);
    return instance;
}

QVariantMap BeeVideoModel::itemMap(const Item &item) const {
    return {
        {QStringLiteral("vodId"), item.id},
        {QStringLiteral("vodName"), item.name},
        {QStringLiteral("vodPic"), item.pic},
        {QStringLiteral("vodRemarks"), item.remarks},
        {QStringLiteral("typeName"), item.type},
        {QStringLiteral("vodYear"), item.year},
        {QStringLiteral("vodArea"), item.area},
        {QStringLiteral("vodClass"), item.vodClass},
        {QStringLiteral("vodBlurb"), item.blurb},
        {QStringLiteral("vodVersion"), item.version}
    };
}

void BeeVideoModel::replaceItems(const QVariantList &items) {
    beginResetModel();
    items_.clear();
    items_.reserve(items.size());
    for (const QVariant &value : items) {
        const QVariantMap map = value.toMap();
        Item item;
        item.id = map.value(QStringLiteral("vodId")).toString();
        item.name = map.value(QStringLiteral("vodName")).toString();
        item.pic = map.value(QStringLiteral("vodPic")).toString();
        item.remarks = map.value(QStringLiteral("vodRemarks")).toString();
        item.type = map.value(QStringLiteral("typeName")).toString();
        item.year = map.value(QStringLiteral("vodYear")).toString();
        item.area = map.value(QStringLiteral("vodArea")).toString();
        item.vodClass = map.value(QStringLiteral("vodClass")).toString();
        item.blurb = map.value(QStringLiteral("vodBlurb")).toString();
        item.version = map.value(QStringLiteral("vodVersion")).toString();
        if (!item.id.isEmpty() && !item.name.isEmpty())
            items_.append(item);
    }
    endResetModel();
    emit countChanged();
}

void BeeVideoModel::fetchItemImages(int requestSerial) {
    constexpr int kImageConcurrency = 4;
    for (int index = 0; index < qMin(kImageConcurrency, items_.size()); ++index)
        fetchItemImage(requestSerial, index);
}

void BeeVideoModel::fetchItemImage(int requestSerial, int index) {
    constexpr int kImageConcurrency = 4;
    if (requestSerial != listRequestSerial_ || index < 0 || index >= items_.size())
        return;
    const QString sourceUrl = items_.at(index).pic;
    if (sourceUrl.isEmpty() || sourceUrl.startsWith(QStringLiteral("data:"))) {
        fetchItemImage(requestSerial, index + kImageConcurrency);
        return;
    }
    client()->fetchImage(sourceUrl,
                         [this, requestSerial, index, sourceUrl](bool ok,
                                                                  const QString &dataUrl,
                                                                  const QString &error) {
            if (requestSerial != listRequestSerial_ || index < 0 || index >= items_.size())
                return;
            if (!ok) {
                // Keep the remote URL as a last-resort fallback. The request
                // failure is logged by BeeClient, but it must not blank a card.
                Q_UNUSED(error);
            } else if (items_.at(index).pic == sourceUrl) {
                items_[index].pic = dataUrl;
                const QModelIndex modelIndex = this->index(index);
                emit dataChanged(modelIndex, modelIndex, {VodPicRole});
                if (detail_.value(QStringLiteral("vodId")).toString() == items_.at(index).id) {
                    detail_.insert(QStringLiteral("vodPic"), dataUrl);
                    emit detailChanged();
                }
            }
            fetchItemImage(requestSerial, index + kImageConcurrency);
        });
}

void BeeVideoModel::loadRecommended(int page) {
    const int requestedPage = qMax(1, page);
    const int requestSerial = ++listRequestSerial_;
    setLoading(true);
    setError({});
    // 近期热播 (mb): 10 cards/page with an explicit total_pages from the
    // server, so hasNext is authoritative rather than guessed from page fill.
    client()->searchRank(requestedPage,
                        [this, requestSerial, requestedPage](bool ok,
                                                               const QVariantList &items,
                                                               int totalPages,
                                                               const QString &error) {
        if (requestSerial != listRequestSerial_)
            return;
        setLoading(false);
        if (!ok) {
            setError(error);
            return;
        }
        setPagination(requestedPage, requestedPage < totalPages && !items.isEmpty());
        replaceItems(items);
        fetchItemImages(requestSerial);
    });
}

void BeeVideoModel::search(const QString &keyword) {
    const QString normalizedKeyword = keyword.trimmed();
    if (normalizedKeyword.isEmpty()) {
        loadRecommended(1);
        return;
    }
    const int requestSerial = ++listRequestSerial_;
    setLoading(true);
    setError({});
    client()->search(normalizedKeyword,
                     [this, requestSerial](bool ok, const QVariantList &items,
                                           const QString &error) {
        if (requestSerial != listRequestSerial_)
            return;
        setLoading(false);
        if (!ok) {
            setError(error);
            return;
        }
        setPagination(1, false);
        replaceItems(items);
        fetchItemImages(requestSerial);
    });
}

void BeeVideoModel::loadDetail(const QString &vodId) {
    const QString normalizedId = vodId.trimmed();
    if (normalizedId.isEmpty())
        return;

    QString expectedName;
    detail_.clear();
    episodes_.clear();
    for (const Item &item : std::as_const(items_)) {
        if (item.id == normalizedId) {
            detail_ = itemMap(item);
            expectedName = item.name.trimmed();
            break;
        }
    }
    detailRequestId_ = normalizedId;
    emit detailChanged();
    setDetailLoading(true);
    setError({});

    client()->detail(normalizedId,
                     [this, normalizedId, expectedName](bool ok, const QVariantMap &detail,
                                                        const QString &error) {
        if (detailRequestId_ != normalizedId)
            return;
        setDetailLoading(false);
        if (!ok) {
            setError(error);
            return;
        }

        const QString detailName = detail.value(QStringLiteral("vodName")).toString().trimmed();
        if (!expectedName.isEmpty() && !detailName.isEmpty()
                && detailName.compare(expectedName, Qt::CaseInsensitive) != 0) {
            // Some Bee list pages expose a stale/wrong vod_id. Re-resolve by
            // the card title before publishing detail or playback episodes.
            setDetailLoading(true);
            client()->search(expectedName,
                [this, expectedName](bool searchOk, const QVariantList &items,
                                     const QString &searchError) {
                    if (!searchOk) {
                        setDetailLoading(false);
                        setError(searchError);
                        return;
                    }
                    QString repairedId;
                    for (const QVariant &value : items) {
                        const QVariantMap candidate = value.toMap();
                        if (candidate.value(QStringLiteral("vodName")).toString().trimmed()
                                .compare(expectedName, Qt::CaseInsensitive) == 0) {
                            repairedId = candidate.value(QStringLiteral("vodId")).toString().trimmed();
                            break;
                        }
                    }
                    if (repairedId.isEmpty()) {
                        setDetailLoading(false);
                        setError(QStringLiteral("蜜蜂详情与卡片标题不一致，未找到同名影片"));
                        return;
                    }
                    loadDetail(repairedId);
                });
            return;
        }

        // Keep the card's identity fields because /vod/play intentionally
        // omits some of them; only merge fields belonging to this request.
        QVariantMap merged = detail_;
        for (auto it = detail.cbegin(); it != detail.cend(); ++it) {
            if (it.key() == QStringLiteral("episodes") || !it.value().toString().isEmpty())
                merged.insert(it.key(), it.value());
        }
        episodes_ = detail.value(QStringLiteral("episodes")).toList();
        merged.insert(QStringLiteral("episodes"), episodes_);
        detail_ = merged;
        emit detailChanged();
    });
}

void BeeVideoModel::clear() {
    ++listRequestSerial_;
    detailRequestId_.clear();
    beginResetModel();
    items_.clear();
    endResetModel();
    detail_.clear();
    episodes_.clear();
    setLoading(false);
    setDetailLoading(false);
    setError({});
    setPagination(1, false);
    emit countChanged();
    emit detailChanged();
}
