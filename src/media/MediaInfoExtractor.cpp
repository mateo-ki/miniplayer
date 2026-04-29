#include "media/MediaInfoExtractor.h"

extern "C" {
#include <libavformat/avformat.h>
}

QVector<MediaInfoItem> MediaInfoExtractor::extract(const QString &path, const AVFormatContext *context) const {
    QVector<MediaInfoItem> items;
    items.push_back({ "File", path });

    if (!context) {
        return items;
    }

    items.push_back({ "Container", context->iformat ? QString::fromUtf8(context->iformat->long_name) : QString("unknown") });
    items.push_back({ "Duration", QString::number(context->duration) });
    items.push_back({ "Bitrate", QString::number(context->bit_rate) });
    return items;
}
