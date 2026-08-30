#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class ApiSiteModel;
class DownloadModel;
#include "models/VideoSearchModel.h"

// 视频站点搜索编排：列表/搜索/详情三个 VideoSearchModel 的驱动与下载排队。
class VideoCatalogController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(VideoSearchModel *videoSearchModel READ videoSearchModel CONSTANT)
    Q_PROPERTY(VideoSearchModel *detailSearchModel READ detailSearchModel CONSTANT)
    Q_PROPERTY(VideoSearchModel *sourceSearchModel READ sourceSearchModel CONSTANT)
public:
    VideoCatalogController(ApiSiteModel *apiSiteModel, DownloadModel *downloadModel,
                           QObject *parent = nullptr);

    VideoSearchModel *videoSearchModel() { return videoSearchModel_; }
    VideoSearchModel *detailSearchModel() { return detailSearchModel_; }
    VideoSearchModel *sourceSearchModel() { return sourceSearchModel_; }

    // 由 AppBootstrap 把播放器当前片名同步过来（换源搜索用）。
    void setCurrentVodName(const QString &name) { currentVodName_ = name; }

    Q_INVOKABLE void searchVideos(const QString &keyword, int page = 1, bool forceRefresh = false);
    Q_INVOKABLE void searchVideoById(const QString &vodId);
    Q_INVOKABLE void loadVideoDetail(int vodId);
    Q_INVOKABLE void loadVideoList(int page = 1);
    Q_INVOKABLE void loadVideoListByCategory(const QString &typeId, int page = 1);
    Q_INVOKABLE void searchCurrentVodOnSite(int siteIndex);
    Q_INVOKABLE void downloadVideoEpisodes(const QString &vodName, const QString &poster, const QVariantList &episodes);
    Q_INVOKABLE void saveVideoM3u8Episodes(const QString &vodName, const QString &poster, const QVariantList &episodes);

private:
    ApiSiteModel *apiSiteModel_;
    DownloadModel *downloadModel_;
    VideoSearchModel *videoSearchModel_;
    VideoSearchModel *detailSearchModel_;
    VideoSearchModel *sourceSearchModel_;
    QString currentVodName_;
};
