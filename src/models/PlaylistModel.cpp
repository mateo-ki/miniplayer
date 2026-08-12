#include "models/PlaylistModel.h"

#include <QFileInfo>
#include <QVariantMap>

PlaylistModel::PlaylistModel(QObject *parent)
    : QAbstractListModel(parent) {}

int PlaylistModel::rowCount(const QModelIndex &) const {
    return items_.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= items_.size())
        return {};

    const auto &item = items_[index.row()];
    switch (role) {
    case FilePathRole:  return item.filePath;
    case TitleRole:     return item.title;
    case LastPlayedRole: return item.lastPlayed.isValid() ? item.lastPlayed.toString("MM-dd HH:mm") : "";
    default:            return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const {
    return {
        { FilePathRole,  "filePath" },
        { TitleRole,     "title" },
        { LastPlayedRole, "lastPlayed" }
    };
}

int PlaylistModel::count() const { return items_.size(); }

void PlaylistModel::addFile(const QString &filePath) {
    addItem(filePath, titleFromPath(filePath));
}

void PlaylistModel::addItem(const QString &filePath, const QString &title) {
    if (filePath.trimmed().isEmpty()) return;
    beginInsertRows(QModelIndex(), items_.size(), items_.size());
    const QString displayTitle = title.trimmed().isEmpty() ? titleFromPath(filePath) : title.trimmed();
    items_.append({ filePath, displayTitle, {} });
    endInsertRows();
    emit countChanged();
}

void PlaylistModel::addHistory(const QString &filePath, const QString &title) {
    if (filePath.trimmed().isEmpty()) return;

    const QString displayTitle = title.trimmed().isEmpty() ? titleFromPath(filePath) : title.trimmed();
    beginResetModel();
    for (int itemIndex = items_.size() - 1; itemIndex >= 0; --itemIndex) {
        if (items_[itemIndex].filePath == filePath) {
            items_.removeAt(itemIndex);
        }
    }
    items_.prepend({ filePath, displayTitle, QDateTime::currentDateTime() });
    constexpr int maxHistoryItems = 100;
    while (items_.size() > maxHistoryItems) {
        items_.removeLast();
    }
    endResetModel();
    emit countChanged();
}

void PlaylistModel::setItems(const QVariantList &items) {
    beginResetModel();
    items_.clear();
    items_.reserve(items.size());
    for (const QVariant &value : items) {
        const QVariantMap map = value.toMap();
        QString filePath = map.value(QStringLiteral("url")).toString();
        if (filePath.trimmed().isEmpty()) {
            filePath = map.value(QStringLiteral("filePath")).toString();
        }
        if (filePath.trimmed().isEmpty()) continue;
        QString title = map.value(QStringLiteral("title")).toString();
        if (title.trimmed().isEmpty()) {
            title = map.value(QStringLiteral("name")).toString();
        }
        if (title.trimmed().isEmpty()) {
            title = titleFromPath(filePath);
        }
        const QDateTime lastPlayed = map.value(QStringLiteral("lastPlayed")).toDateTime();
        items_.append({ filePath, title, lastPlayed });
    }
    endResetModel();
    emit countChanged();
}

QVariantList PlaylistModel::toVariantList() const {
    QVariantList result;
    result.reserve(items_.size());
    for (const PlaylistItem &item : items_) {
        QVariantMap map;
        map[QStringLiteral("filePath")] = item.filePath;
        map[QStringLiteral("title")] = item.title;
        map[QStringLiteral("lastPlayed")] = item.lastPlayed;
        result.append(map);
    }
    return result;
}

void PlaylistModel::removeAt(int index) {
    if (index < 0 || index >= items_.size()) return;
    beginRemoveRows(QModelIndex(), index, index);
    items_.removeAt(index);
    endRemoveRows();
    emit countChanged();
}

void PlaylistModel::move(int from, int to) {
    if (from < 0 || from >= items_.size()) return;
    if (to < 0 || to >= items_.size()) return;
    if (from == to) return;

    int destRow = to > from ? to + 1 : to;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), destRow)) return;
    auto item = items_.takeAt(from);
    items_.insert(to, item);
    endMoveRows();
}

void PlaylistModel::clear() {
    if (items_.isEmpty()) return;
    beginResetModel();
    items_.clear();
    endResetModel();
    emit countChanged();
}

QString PlaylistModel::filePathAt(int index) const {
    if (index < 0 || index >= items_.size()) return {};
    return items_[index].filePath;
}

QString PlaylistModel::titleAt(int index) const {
    if (index < 0 || index >= items_.size()) return {};
    return items_[index].title;
}

int PlaylistModel::indexOfPath(const QString &filePath) const {
    for (int i = 0; i < items_.size(); ++i) {
        if (items_[i].filePath == filePath) return i;
    }
    return -1;
}

QString PlaylistModel::titleFromPath(const QString &path) {
    return QFileInfo(path).fileName();
}
