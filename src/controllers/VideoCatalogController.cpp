#include "controllers/VideoCatalogController.h"

#include <QUrl>

#include "models/ApiSiteModel.h"
#include "models/DownloadModel.h"
#include "models/VideoSearchModel.h"

#include <QCoreApplication>
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
