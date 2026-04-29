#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct RuntimeLogEntry {
    QString level;
    QString message;
};

class RuntimeLogModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        LevelRole = Qt::UserRole + 1,
        MessageRole
    };

    explicit RuntimeLogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    void append(const QString &level, const QString &message);

private:
    QVector<RuntimeLogEntry> entries_;
};
