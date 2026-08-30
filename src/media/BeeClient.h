#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class BeeClient final {
public:
    explicit BeeClient(QObject *parent = nullptr);

    void recommended(int page, int limit,
                     const std::function<void(bool, const QVariantList &, const QString &)> &callback);
    void searchRank(int page,
                    const std::function<void(bool, const QVariantList &, int totalPages, const QString &)> &callback);
    void filmSchedule(const QString &tag,
                      const std::function<void(bool, const QVariantList &, const QString &)> &callback);
    void search(const QString &keyword,
                const std::function<void(bool, const QVariantList &, const QString &)> &callback);
    void detail(const QString &vodId,
                const std::function<void(bool, const QVariantMap &, const QString &)> &callback);
    void fetchImage(const QString &url,
                    const std::function<void(bool, const QString &, const QString &)> &callback);

private:
    QNetworkAccessManager nam_;
};
