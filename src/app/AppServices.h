#pragma once

#include <QObject>

#include "controller/AuthController.h"
#include "controller/ImageController.h"
#include "controller/MemeController.h"
#include "controller/PoolChatController.h"
#include "controller/PoolMeController.h"
#include "controller/UpdateController.h"
#include "controller/VideoCatalogController.h"
#include "controller/VoiceController.h"
#include "models/ApiSiteModel.h"
#include "models/DmghgAnimeModel.h"
#include "models/DownloadModel.h"

// 功能模型容器（组合根）。
// 各业务模块的模型统一在此持有并暴露给 QML（services 上下文属性），
// PlayerController 通过构造注入只借用它需要的模型，从而与具体功能解耦：
// 新增功能模型时不再需要往 PlayerController 上挂属性。
class AppServices final : public QObject {
    Q_OBJECT
    Q_PROPERTY(AuthController *authController READ authController CONSTANT)
    Q_PROPERTY(ApiSiteModel *apiSiteModel READ apiSiteModel CONSTANT)
    Q_PROPERTY(DmghgAnimeModel *dmghgAnimeModel READ dmghgAnimeModel CONSTANT)
    Q_PROPERTY(DownloadModel *downloadModel READ downloadModel CONSTANT)
    Q_PROPERTY(VideoCatalogController *videoCatalog READ videoCatalog CONSTANT)
    Q_PROPERTY(ImageController *imageController READ imageController CONSTANT)
    Q_PROPERTY(MemeController *memeController READ memeController CONSTANT)
    Q_PROPERTY(VoiceController *voiceController READ voiceController CONSTANT)
    Q_PROPERTY(UpdateController *updateController READ updateController CONSTANT)
    Q_PROPERTY(PoolMeController *poolMe READ poolMe CONSTANT)
    Q_PROPERTY(PoolChatController *poolChat READ poolChat CONSTANT)
public:
    explicit AppServices(QObject *parent = nullptr);

    AuthController *authController() const { return authController_; }
    ApiSiteModel *apiSiteModel() { return &apiSiteModel_; }
    DmghgAnimeModel *dmghgAnimeModel() { return &dmghgAnimeModel_; }
    DownloadModel *downloadModel() { return &downloadModel_; }
    VideoCatalogController *videoCatalog() { return videoCatalogController_; }
    ImageController *imageController() { return imageController_; }
    MemeController *memeController() { return memeController_; }
    VoiceController *voiceController() { return voiceController_; }
    UpdateController *updateController() { return updateController_; }
    PoolMeController *poolMe() { return poolMeController_; }
    PoolChatController *poolChat() { return poolChatController_; }

private:
    AuthController *authController_;
    ApiSiteModel apiSiteModel_;
    DmghgAnimeModel dmghgAnimeModel_;
    DownloadModel downloadModel_;
    VideoCatalogController *videoCatalogController_;
    ImageController *imageController_;
    MemeController *memeController_;
    VoiceController *voiceController_;
    UpdateController *updateController_;
    PoolMeController *poolMeController_;
    PoolChatController *poolChatController_;
};
