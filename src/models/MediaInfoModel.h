#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct MediaInfoItem {
    QString key;
    QString value;
};

class MediaInfoModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        ValueRole
    };

    explicit MediaInfoModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replaceAll(const QVector<MediaInfoItem> &items);
    const QVector<MediaInfoItem> &items() const { return items_; }

private:
    QVector<MediaInfoItem> items_;
};
