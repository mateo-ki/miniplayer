#pragma once

#include <QString>
#include <QVector>

#include "models/MediaInfoModel.h"

struct AVFormatContext;

class MediaInfoExtractor {
public:
    QVector<MediaInfoItem> extract(const QString &path, const AVFormatContext *context) const;
};
