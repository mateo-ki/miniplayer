#include "media/MediaInfoExtractor.h"

#include "infrastructure/Logger.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
}

QVector<MediaInfoItem> MediaInfoExtractor::extract(const QString &path, const AVFormatContext *context) const {
    QVector<MediaInfoItem> items;
    items.push_back({ "File", path });

    if (!context) {
        return items;
    }

    items.push_back({ "Container", context->iformat ? QString::fromUtf8(context->iformat->long_name) : QString("unknown") });

    if (context->duration > 0) {
        const double durationSec = static_cast<double>(context->duration) / AV_TIME_BASE;
        const int mins = static_cast<int>(durationSec) / 60;
        const int secs = static_cast<int>(durationSec) % 60;
        items.push_back({ "Duration", QString("%1:%2").arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0')) });
    }

    if (context->bit_rate > 0) {
        items.push_back({ "Bitrate", QString::number(context->bit_rate / 1000) + " kbps" });
    }

    for (unsigned i = 0; i < context->nb_streams; ++i) {
        const AVStream *stream = context->streams[i];
        if (!stream || !stream->codecpar) continue;

        const AVCodecParameters *par = stream->codecpar;
        const char *codecName = avcodec_get_name(par->codec_id);
        const QString prefix = par->codec_type == AVMEDIA_TYPE_VIDEO ? "Video" : "Audio";

        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            items.push_back({ prefix + " Codec", QString::fromUtf8(codecName) });
            items.push_back({ "Resolution", QString("%1x%2").arg(par->width).arg(par->height) });

            if (stream->avg_frame_rate.den > 0) {
                const double fps = av_q2d(stream->avg_frame_rate);
                items.push_back({ "Frame Rate", QString::number(fps, 'f', 2) + " fps" });
            }

            const char *pixFmt = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format));
            if (pixFmt) {
                items.push_back({ "Pixel Format", QString::fromUtf8(pixFmt) });
            }

            Logger::instance().info("Extracted video metadata: " + QString::fromUtf8(codecName)
                + " " + QString::number(par->width) + "x" + QString::number(par->height));
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            items.push_back({ prefix + " Codec", QString::fromUtf8(codecName) });
            items.push_back({ "Sample Rate", QString::number(par->sample_rate) + " Hz" });
            items.push_back({ "Channels", QString::number(par->ch_layout.nb_channels) });

            const char *sampleFmt = av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format));
            if (sampleFmt) {
                items.push_back({ "Sample Format", QString::fromUtf8(sampleFmt) });
            }

            Logger::instance().info("Extracted audio metadata: " + QString::fromUtf8(codecName)
                + " " + QString::number(par->sample_rate) + "Hz "
                + QString::number(par->ch_layout.nb_channels) + "ch");
        }
    }

    return items;
}
