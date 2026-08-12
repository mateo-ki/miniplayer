#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QDateTime>
#include <QVariantList>

struct PlaylistItem {
    QString filePath;
    QString title;
    QDateTime lastPlayed;
};

class PlaylistModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        FilePathRole = Qt::UserRole + 1,
        TitleRole,
        LastPlayedRole
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    Q_INVOKABLE void addFile(const QString &filePath);
    Q_INVOKABLE void addItem(const QString &filePath, const QString &title);
    Q_INVOKABLE void addHistory(const QString &filePath, const QString &title);
    Q_INVOKABLE void setItems(const QVariantList &items);
    Q_INVOKABLE QVariantList toVariantList() const;
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString filePathAt(int index) const;
    Q_INVOKABLE QString titleAt(int index) const;
    int indexOfPath(const QString &filePath) const;

signals:
    void countChanged();

private:
    QVector<PlaylistItem> items_;
    static QString titleFromPath(const QString &path);
};
