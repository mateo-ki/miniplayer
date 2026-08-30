#pragma once

#include <QObject>

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
public:
    explicit AppServices(QObject *parent = nullptr);

    ApiSiteModel *apiSiteModel() { return &apiSiteModel_; }
    DmghgAnimeModel *dmghgAnimeModel() { return &dmghgAnimeModel_; }
    BeeVideoModel *beeVideoModel() { return &beeVideoModel_; }
    BeeScheduleModel *beeScheduleModel() { return &beeScheduleModel_; }
    DownloadModel *downloadModel() { return &downloadModel_; }
    LegadoSourceModel *legadoSourceModel() { return &legadoSourceModel_; }
    LegadoBookModel *legadoBookModel() { return &legadoBookModel_; }

private:
    void wireInternalLinks();

    ApiSiteModel apiSiteModel_;
    DmghgAnimeModel dmghgAnimeModel_;
    BeeVideoModel beeVideoModel_;
    BeeScheduleModel beeScheduleModel_;
    DownloadModel downloadModel_;
    LegadoSourceModel legadoSourceModel_;
    LegadoBookModel legadoBookModel_;
};
