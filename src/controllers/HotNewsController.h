#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

// 热讯聚合模块。
class HotNewsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hotNewsLoading READ hotNewsLoading NOTIFY hotNewsChanged)
    Q_PROPERTY(QVariantList hotNewsItems READ hotNewsItems NOTIFY hotNewsChanged)
    Q_PROPERTY(QString hotNewsMessage READ hotNewsMessage NOTIFY hotNewsChanged)
public:
    explicit HotNewsController(QObject *parent = nullptr);

    bool hotNewsLoading() const;
    QVariantList hotNewsItems() const;
    QString hotNewsMessage() const;

    Q_INVOKABLE void loadHotNews(const QString &type = QStringLiteral("baidu"));

signals:
    void hotNewsChanged();

private:
    bool hotNewsLoading_ = false;
    QVariantList hotNewsItems_;
    QString hotNewsMessage_;
};
