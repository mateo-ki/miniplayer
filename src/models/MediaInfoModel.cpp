#include "models/MediaInfoModel.h"

MediaInfoModel::MediaInfoModel(QObject *parent)
    : QAbstractListModel(parent) {}

int MediaInfoModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant MediaInfoModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    const MediaInfoItem &item = items_.at(index.row());
    switch (role) {
    case KeyRole:
        return item.key;
    case ValueRole:
        return item.value;
    default:
        return {};
    }
}

QHash<int, QByteArray> MediaInfoModel::roleNames() const {
    return {
        { KeyRole, "key" },
        { ValueRole, "value" }
    };
}

void MediaInfoModel::replaceAll(const QVector<MediaInfoItem> &items) {
    beginResetModel();
    items_ = items;
    endResetModel();
}
