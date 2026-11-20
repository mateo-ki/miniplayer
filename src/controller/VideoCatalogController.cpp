#include "controller/VideoCatalogController.h"

#include <QUrl>

#include "models/ApiSiteModel.h"
#include "models/DownloadModel.h"
#include "models/VideoSearchModel.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrlQuery>

#include "common/MediaUtils.h"
#include "infrastructure/Logger.h"

VideoCatalogController::VideoCatalogController(ApiSiteModel *apiSiteModel,
                                               DownloadModel *downloadModel,
                                               QObject *parent)
    : QObject(parent),
      apiSiteModel_(apiSiteModel),
      downloadModel_(downloadModel),
      videoSearchModel_(new VideoSearchModel(this)),
      detailSearchModel_(new VideoSearchModel(this)),
      sourceSearchModel_(new VideoSearchModel(this)) {
    // 站点切换后清空搜索结果并加载默认列表（原 PlayerController 接线）。
    connect(apiSiteModel_, &ApiSiteModel::currentIndexChanged, this, [this]() {
        Logger::instance().info("API site changed, loading default VOD list: "
            + apiSiteModel_->currentBaseUrl());
        videoSearchModel_->clear();
        loadVideoList(1);
    });
    // 全新安装: 站点由 Gitee 异步引导 (从无到有), currentIndex 保持 0 不会
    // 触发上面的 currentIndexChanged。站点数从 0 变正时补一次默认列表加载,
    // 否则视频菜单要等用户手动换站才有内容。
    connect(apiSiteModel_, &ApiSiteModel::countChanged, this, [this]() {
        if (apiSiteModel_->count() > 0
                && videoSearchModel_->count() == 0
                && !apiSiteModel_->currentVideoBaseUrl().isEmpty()) {
            Logger::instance().info("Sites arrived from empty, loading default VOD list: "
                + apiSiteModel_->currentVideoBaseUrl());
            loadVideoList(1);
        }
    });

    scheduleDays_ = loadScheduleFromDisk();
    const int todayWd = QDate::currentDate().dayOfWeek();
    currentScheduleDay_ = (todayWd >= 1 && todayWd <= 7) ? todayWd : 1;
    if (!scheduleDays_.isEmpty()) {
        Logger::instance().info(QStringLiteral("loadWeeklySchedule: restored %1 days from local cache")
            .arg(scheduleDays_.size()));
    }
    connect(apiSiteModel_, &ApiSiteModel::poolCredentialsChanged, this, [this]() {
        lastScheduleFetchSecs_ = 0; // 凭据就绪时允许立即静默刷新一次
        loadWeeklySchedule();
    });
    loadWeeklySchedule();
}
void VideoCatalogController::searchCurrentVodOnSite(int siteIndex) {
    if (currentVodName_.trimmed().isEmpty()) {
        Logger::instance().warn("No current VOD name for source search");
        return;
    }

    const QString baseUrl = apiSiteModel_->baseUrlAt(siteIndex);
    if (baseUrl.isEmpty()) {
        Logger::instance().error("Invalid API site index for source search: " + QString::number(siteIndex));
        return;
    }

    Logger::instance().info("Source search: " + currentVodName_ + " on " + baseUrl);
    sourceSearchModel_->search(baseUrl, currentVodName_, 1);
}

void VideoCatalogController::downloadVideoEpisodes(const QString &vodName, const QString &poster, const QVariantList &episodes) {
    downloadModel_->enqueueVideo(vodName, poster, episodes);
}

void VideoCatalogController::saveVideoM3u8Episodes(const QString &vodName, const QString &poster, const QVariantList &episodes) {
    downloadModel_->enqueueVideoM3u8Only(vodName, poster, episodes);
}

void VideoCatalogController::searchVideos(const QString &keyword, int page, bool forceRefresh) {
    QString baseUrl = apiSiteModel_->currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Searching: " + keyword + " on " + baseUrl);
    videoSearchModel_->search(baseUrl, keyword, page, forceRefresh);
}

void VideoCatalogController::searchVideoById(const QString &vodId) {
    QString baseUrl = apiSiteModel_->currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Searching by ID: " + vodId);
    videoSearchModel_->searchById(baseUrl, vodId);
}

void VideoCatalogController::loadVideoDetail(int vodId) {
    if (vodId <= 0) {
        Logger::instance().warn("Invalid VOD id for detail: " + QString::number(vodId));
        return;
    }
    QString baseUrl = apiSiteModel_->currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Loading VOD detail: " + QString::number(vodId) + " on " + baseUrl);
    detailSearchModel_->searchById(baseUrl, QString::number(vodId));
}

void VideoCatalogController::loadVideoDetailByKey(const QString &vodKey) {
    if (vodKey.trimmed().isEmpty()) {
        Logger::instance().warn("Invalid VOD key for detail (empty)");
        return;
    }
    QString baseUrl = apiSiteModel_->currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Loading VOD detail by key: " + vodKey + " on " + baseUrl);
    detailSearchModel_->searchById(baseUrl, vodKey);
}

void VideoCatalogController::loadVideoList(int page) {
    QString baseUrl = apiSiteModel_->currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Loading video list page " + QString::number(page));
    videoSearchModel_->loadList(baseUrl, page);
}

void VideoCatalogController::loadVideoListByCategory(const QString &typeId, int page) {
    QString baseUrl = apiSiteModel_->currentVideoBaseUrl();
    if (baseUrl.isEmpty()) {
        Logger::instance().error("No API site configured");
        return;
    }
    Logger::instance().info("Loading video list page " + QString::number(page)
        + " category " + typeId);
    videoSearchModel_->loadList(baseUrl, page, typeId);
}

void VideoCatalogController::setCurrentScheduleDay(int day) {
    if (day < 1 || day > 7 || currentScheduleDay_ == day) return;
    currentScheduleDay_ = day;
    emit currentScheduleDayChanged();
}

static QString scheduleCacheFilePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/weekly_schedule_cache.json");
}

void VideoCatalogController::saveScheduleToDisk(const QVariantMap &daysMap) {
    if (daysMap.isEmpty()) return;
    const QString path = scheduleCacheFilePath();
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QJsonDocument doc = QJsonDocument::fromVariant(daysMap);
        file.write(doc.toJson(QJsonDocument::Compact));
        file.close();
    }
}

QVariantMap VideoCatalogController::loadScheduleFromDisk() {
    const QString path = scheduleCacheFilePath();
    QFile file(path);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        file.close();
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            return doc.toVariant().toMap();
        }
    }
    return {};
}

void VideoCatalogController::loadWeeklySchedule() {
    if (scheduleLoading_) return;
    const qint64 nowSecs = QDateTime::currentSecsSinceEpoch();
    if (!scheduleDays_.isEmpty() && (nowSecs - lastScheduleFetchSecs_ < 300)) {
        return; // 5 分钟内拉取过，直接复用内存/磁盘缓存
    }

    scheduleLoading_ = true;
    emit scheduleLoadingChanged();

    QString server;
    QString token;
    if (apiSiteModel_) {
        server = apiSiteModel_->poolServerUrl().trimmed();
        token = apiSiteModel_->poolSessionToken().trimmed();
    }
    if (server.isEmpty() || token.isEmpty()) {
        QSettings s(QStringLiteral("MeloBox"), QStringLiteral("MeloBox"));
        if (server.isEmpty()) {
            server = s.value(QStringLiteral("auth/serverUrl"), QStringLiteral("https://pool.983698.xyz")).toString().trimmed();
        }
        if (token.isEmpty()) {
            token = s.value(QStringLiteral("auth/sessionToken")).toString().trimmed();
        }
    }
    if (server.isEmpty()) {
        server = QStringLiteral("https://pool.983698.xyz");
    }

    Logger::instance().info(QStringLiteral("loadWeeklySchedule starting: server=%1, hasToken=%2")
        .arg(server).arg(!token.isEmpty()));

    QString path = QStringLiteral("/api/movie/schedule");
    if (!token.isEmpty()) {
        path += QStringLiteral("?token=") + QString::fromUtf8(QUrl::toPercentEncoding(token));
    }
    const QUrl url(server + path);

    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setTransferTimeout(6000); // 6秒超时，防止网络死锁
    request.setRawHeader("User-Agent", QByteArrayLiteral("Mozilla/5.0 MeloBox/1.0"));
    request.setRawHeader("Accept", QByteArrayLiteral("application/json"));
    if (!token.isEmpty()) {
        request.setRawHeader("Cookie", QStringLiteral("vpn_session=%1; token=%1").arg(token).toUtf8());
    }

    QNetworkReply *reply = nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, server, token]() {
        reply->deleteLater();
        nam->deleteLater();

        bool success = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray body = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isObject()) {
                const QJsonObject root = doc.object();
                if (root.value(QStringLiteral("ok")).toBool()) {
                    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
                    const QJsonObject daysObj = data.value(QStringLiteral("days")).toObject();
                    QVariantMap daysMap;
                    int totalItems = 0;
                    for (int d = 1; d <= 7; ++d) {
                        const QString key = QString::number(d);
                        const QJsonArray arr = daysObj.value(key).toArray();
                        QVariantList list;
                        for (const QJsonValue &v : arr) {
                            const QJsonObject obj = v.toObject();
                            QVariantMap card;
                            const QString idStr = obj.value(QStringLiteral("id")).toString();
                            card.insert(QStringLiteral("vodId"), 0);
                            card.insert(QStringLiteral("vodIdStr"), idStr);
                            card.insert(QStringLiteral("vodName"), obj.value(QStringLiteral("name")).toString(obj.value(QStringLiteral("title")).toString()));
                            card.insert(QStringLiteral("vodPic"), obj.value(QStringLiteral("pic")).toString(obj.value(QStringLiteral("poster_url")).toString()));
                            card.insert(QStringLiteral("vodRemarks"), obj.value(QStringLiteral("continu")).toString(obj.value(QStringLiteral("remarks")).toString()));
                            card.insert(QStringLiteral("schedule"), obj.value(QStringLiteral("schedule")).toString());
                            card.insert(QStringLiteral("typeName"), obj.value(QStringLiteral("type")).toString(obj.value(QStringLiteral("category")).toString()));
                            card.insert(QStringLiteral("vodYear"), obj.value(QStringLiteral("year")).toString());
                            list.append(card);
                        }
                        daysMap.insert(key, list);
                        totalItems += list.size();
                    }
                    scheduleDays_ = daysMap;
                    lastScheduleFetchSecs_ = QDateTime::currentSecsSinceEpoch();
                    saveScheduleToDisk(daysMap);
                    success = true;
                    Logger::instance().info(QStringLiteral("loadWeeklySchedule success: %1 items loaded across 7 days").arg(totalItems));
                    emit scheduleDaysChanged();
                } else {
                    Logger::instance().warn("loadWeeklySchedule root.ok is false");
                }
            } else {
                Logger::instance().warn("loadWeeklySchedule json doc is not object");
            }
        } else {
            Logger::instance().warn(QStringLiteral("loadWeeklySchedule network error: %1, status=%2")
                .arg(reply->errorString())
                .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
        }

        // 容灾降级：若 /api/movie/schedule 异常，尝试拉取 /api/movie/list?page=1 首页推荐流分拆
        if (!success && !server.isEmpty()) {
            Logger::instance().info("loadWeeklySchedule falling back to /api/movie/list?page=1");
            auto *fallbackNam = new QNetworkAccessManager(this);
            QUrl fallbackUrl(server + QStringLiteral("/api/movie/list?page=1"));
            QNetworkRequest fallbackReq(fallbackUrl);
            fallbackReq.setTransferTimeout(6000); // 6秒超时
            fallbackReq.setRawHeader("User-Agent", QByteArrayLiteral("Mozilla/5.0 MeloBox/1.0"));
            fallbackReq.setRawHeader("Accept", QByteArrayLiteral("application/json"));
            if (!token.isEmpty()) {
                fallbackReq.setRawHeader("Cookie", QStringLiteral("vpn_session=%1; token=%1").arg(token).toUtf8());
            }
            QNetworkReply *fallbackReply = fallbackNam->get(fallbackReq);
            connect(fallbackReply, &QNetworkReply::finished, this, [this, fallbackReply, fallbackNam]() {
                fallbackReply->deleteLater();
                fallbackNam->deleteLater();
                if (fallbackReply->error() == QNetworkReply::NoError) {
                    const QJsonDocument doc = QJsonDocument::fromJson(fallbackReply->readAll());
                    const QJsonObject root = doc.object();
                    if (root.value(QStringLiteral("ok")).toBool()) {
                        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
                        const QJsonArray sections = data.value(QStringLiteral("sections")).toArray();
                        QVariantMap daysMap;
                        for (int d = 1; d <= 7; ++d) {
                            daysMap.insert(QString::number(d), QVariantList());
                        }
                        int cardIdx = 0;
                        const QStringList dayNames = {
                            QStringLiteral("周一更"), QStringLiteral("周二更"), QStringLiteral("周三更"),
                            QStringLiteral("周四更"), QStringLiteral("周五更"), QStringLiteral("周六更"),
                            QStringLiteral("周日更")
                        };
                        for (const QJsonValue &secVal : sections) {
                            const QJsonObject secObj = secVal.toObject();
                            const QJsonArray cards = secObj.value(QStringLiteral("cards")).toArray();
                            for (const QJsonValue &cardVal : cards) {
                                const QJsonObject obj = cardVal.toObject();
                                const int targetDay = (cardIdx % 7) + 1;
                                const QString key = QString::number(targetDay);
                                QVariantList curList = daysMap.value(key).toList();
                                QVariantMap card;
                                card.insert(QStringLiteral("vodId"), 0);
                                card.insert(QStringLiteral("vodIdStr"), obj.value(QStringLiteral("id")).toString());
                                card.insert(QStringLiteral("vodName"), obj.value(QStringLiteral("title")).toString());
                                card.insert(QStringLiteral("vodPic"), obj.value(QStringLiteral("poster_url")).toString());
                                card.insert(QStringLiteral("vodRemarks"), obj.value(QStringLiteral("remarks")).toString());
                                card.insert(QStringLiteral("schedule"), dayNames[targetDay - 1]);
                                const QJsonArray genres = obj.value(QStringLiteral("genres")).toArray();
                                card.insert(QStringLiteral("typeName"), genres.isEmpty() ? obj.value(QStringLiteral("area")).toString() : genres.first().toString());
                                card.insert(QStringLiteral("vodYear"), QString::number(obj.value(QStringLiteral("year")).toInt()));
                                curList.append(card);
                                daysMap.insert(key, curList);
                                ++cardIdx;
                            }
                        }
                        scheduleDays_ = daysMap;
                        lastScheduleFetchSecs_ = QDateTime::currentSecsSinceEpoch();
                        saveScheduleToDisk(daysMap);
                        Logger::instance().info(QStringLiteral("loadWeeklySchedule fallback success: %1 cards loaded").arg(cardIdx));
                        emit scheduleDaysChanged();
                    }
                }
                scheduleLoading_ = false;
                emit scheduleLoadingChanged();
            });
            return;
        }

        scheduleLoading_ = false;
        emit scheduleLoadingChanged();
    });
}
