#include "models/RuntimeLogModel.h"

RuntimeLogModel::RuntimeLogModel(QObject *parent)
    : QAbstractListModel(parent) {}

int RuntimeLogModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant RuntimeLogModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    const RuntimeLogEntry &entry = entries_.at(index.row());
    switch (role) {
    case LevelRole:
        return entry.level;
    case MessageRole:
        return entry.message;
    default:
        return {};
    }
}

QHash<int, QByteArray> RuntimeLogModel::roleNames() const {
    return {
        { LevelRole, "level" },
        { MessageRole, "message" }
    };
}

void RuntimeLogModel::clear() {
    beginResetModel();
    entries_.clear();
    endResetModel();
}

void RuntimeLogModel::append(const QString &level, const QString &message) {
    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    entries_.push_back({ level, message });
    endInsertRows();
}
