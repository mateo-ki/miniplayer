#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

// 表情包模块：搜索、随机龙图、下载与复制。
class MemeController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool memeLoading READ memeLoading NOTIFY memeStateChanged)
    Q_PROPERTY(QVariantList memeImages READ memeImages NOTIFY memeStateChanged)
    Q_PROPERTY(QString memeMessage READ memeMessage NOTIFY memeStateChanged)
public:
    explicit MemeController(QObject *parent = nullptr);

    bool memeLoading() const;
    QVariantList memeImages() const;
    QString memeMessage() const;

    Q_INVOKABLE void searchMemes(const QString &keyword = QStringLiteral("龙图"), int count = 10);
    Q_INVOKABLE void loadDragonMeme();
    Q_INVOKABLE void saveMemeImage(const QString &imageUrl);
    Q_INVOKABLE void copyMemeUrl(const QString &imageUrl);

signals:
    void memeStateChanged();

private:
    bool memeLoading_ = false;
    QVariantList memeImages_;
    QString memeMessage_;
};
