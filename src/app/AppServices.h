#pragma once

#include <QObject>

#include "controllers/HotNewsController.h"
#include "controllers/ImageController.h"
#include "controllers/MemeController.h"
#include "controllers/UpdateController.h"
#include "controllers/VideoCatalogController.h"
#include "controllers/VoiceController.h"
#include "models/ApiSiteModel.h"
#include "models/BeeScheduleModel.h"
#include "models/BeeVideoModel.h"
#include "models/DmghgAnimeModel.h"
#include "models/DownloadModel.h"
#include "models/LegadoBookModel.h"
#include "models/LegadoSourceModel.h"

// 功能模型容器（组合根）。
// 各业务模块的模型统一在此持有并暴露给 QML（services 上下文属性），
// PlayerController 通过构造注入只借用它需要的模型，从而与具体功能解耦：
// 新增功能模型时不再需要往 PlayerController 上挂属性。
class AppServices final : public QObject {
    Q_OBJECT
    Q_PROPERTY(ApiSiteModel *apiSiteModel READ apiSiteModel CONSTANT)
    Q_PROPERTY(DmghgAnimeModel *dmghgAnimeModel READ dmghgAnimeModel CONSTANT)
    Q_PROPERTY(BeeVideoModel *beeVideoModel READ beeVideoModel CONSTANT)
    Q_PROPERTY(BeeScheduleModel *beeScheduleModel READ beeScheduleModel CONSTANT)
    Q_PROPERTY(DownloadModel *downloadModel READ downloadModel CONSTANT)
    Q_PROPERTY(LegadoSourceModel *legadoSourceModel READ legadoSourceModel CONSTANT)
    Q_PROPERTY(LegadoBookModel *legadoBookModel READ legadoBookModel CONSTANT)
    Q_PROPERTY(VideoCatalogController *videoCatalog READ videoCatalog CONSTANT)
    Q_PROPERTY(ImageController *imageController READ imageController CONSTANT)
    Q_PROPERTY(MemeController *memeController READ memeController CONSTANT)
    Q_PROPERTY(VoiceController *voiceController READ voiceController CONSTANT)
    Q_PROPERTY(HotNewsController *hotNewsController READ hotNewsController CONSTANT)
    Q_PROPERTY(UpdateController *updateController READ updateController CONSTANT)
public:
    explicit AppServices(QObject *parent = nullptr);

    ApiSiteModel *apiSiteModel() { return &apiSiteModel_; }
    DmghgAnimeModel *dmghgAnimeModel() { return &dmghgAnimeModel_; }
    BeeVideoModel *beeVideoModel() { return &beeVideoModel_; }
    BeeScheduleModel *beeScheduleModel() { return &beeScheduleModel_; }
    DownloadModel *downloadModel() { return &downloadModel_; }
    LegadoSourceModel *legadoSourceModel() { return &legadoSourceModel_; }
    LegadoBookModel *legadoBookModel() { return &legadoBookModel_; }
    VideoCatalogController *videoCatalog() { return videoCatalogController_; }
    ImageController *imageController() { return imageController_; }
    MemeController *memeController() { return memeController_; }
    VoiceController *voiceController() { return voiceController_; }
    HotNewsController *hotNewsController() { return hotNewsController_; }
    UpdateController *updateController() { return updateController_; }

private:
    void wireInternalLinks();

    ApiSiteModel apiSiteModel_;
    DmghgAnimeModel dmghgAnimeModel_;
    BeeVideoModel beeVideoModel_;
    BeeScheduleModel beeScheduleModel_;
    DownloadModel downloadModel_;
    LegadoSourceModel legadoSourceModel_;
    LegadoBookModel legadoBookModel_;
    VideoCatalogController *videoCatalogController_;
    ImageController *imageController_;
    MemeController *memeController_;
    VoiceController *voiceController_;
    HotNewsController *hotNewsController_;
    UpdateController *updateController_;
};
