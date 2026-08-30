#pragma once

#include <QAbstractListModel>
#include <QNetworkAccessManager>
#include <QString>
#include <QVector>

#include "book/BookSource.h"

// Legado 书源列表：导入（URL / 粘贴 JSON）、去重、持久化与选中管理。
// 书源文件通常是 1248.json 这类数组（可含数百个源），也兼容单个对象。
class LegadoSourceModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(bool hasSelected READ hasSelected NOTIFY selectedIndexChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        UrlRole,
        GroupRole,
        TypeRole,
        EnabledRole,
        SelectedRole,
        CommentRole
    };

    explicit LegadoSourceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool importing() const;
    QString message() const;
    int selectedIndex() const;
    bool hasSelected() const;

    BookSource sourceAt(int row) const;
    BookSource selectedSource() const;
    QString sourceVariable(int row) const;
    void setSourceVariable(int row, const QString &variable);

    Q_INVOKABLE void importFromUrl(const QString &url);
    Q_INVOKABLE void importFromText(const QString &text);
    // 文本框为空时的一键导入：读系统剪贴板；内容是链接则自动转下载
    Q_INVOKABLE void importFromClipboard();
    Q_INVOKABLE void removeSource(int row);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void selectSource(int row);
    Q_INVOKABLE void setEnabled(int row, bool enabled);

signals:
    void countChanged();
    void importingChanged();
    void messageChanged();
    void selectedIndexChanged();
    void activeSourceChanged();

private:
    void loadPersisted();
    void persist() const;
    int upsertSource(const BookSource &source);
    void mergeJson(const QByteArray &bytes, const QString &origin);
    void setLoading(bool loading);
    void setMessage(const QString &message);
    void setSelectedIndex(int index);
    QString storagePath() const;

    QVector<BookSource> sources_;
    int selectedIndex_ = -1;
    bool importing_ = false;
    QString message_;
    QNetworkAccessManager nam_;
};
