#include "media/DemuxWorker.h"

#include "infrastructure/Logger.h"
#include "media/PacketQueue.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

void DemuxWorker::configure(AVFormatContext *ctx, PacketQueue *audioQueue,
                            PacketQueue *videoQueue, PacketQueue *subtitleQueue,
                            int audioStreamIndex, int videoStreamIndex, int subtitleStreamIndex) {
    ctx_ = ctx;
    audioQueue_ = audioQueue;
    videoQueue_ = videoQueue;
    subtitleQueue_ = subtitleQueue;
    audioStreamIndex_ = audioStreamIndex;
    videoStreamIndex_ = videoStreamIndex;
    subtitleStreamIndex_ = subtitleStreamIndex;
}

void DemuxWorker::start() {
    Logger::instance().info("DemuxWorker started, audio stream #"
        + QString::number(audioStreamIndex_) + ", video stream #"
        + QString::number(videoStreamIndex_));

    AVPacket *pkt = av_packet_alloc();
    int packetCount = 0;

    while (!aborted_) {
        int ret = av_read_frame(ctx_, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                Logger::instance().info("DemuxWorker: EOF reached, total packets=" + QString::number(packetCount));
                break;
            }
            char errBuf[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(ret, errBuf, sizeof(errBuf));
            Logger::instance().warn("DemuxWorker: av_read_frame error (continuing): "
                + QString::fromUtf8(errBuf));
            continue;
        }

        ++packetCount;
        if (pkt->stream_index == audioStreamIndex_) {
            audioQueue_->push(pkt);
        } else if (pkt->stream_index == videoStreamIndex_) {
            videoQueue_->push(pkt);
        } else if (subtitleQueue_ && pkt->stream_index == subtitleStreamIndex_) {
            subtitleQueue_->push(pkt);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    // Signal EOF — push a sentinel null packet so workers know to drain and stop
    // Don't abort queues; workers need to drain remaining packets first
    if (audioStreamIndex_ >= 0) audioQueue_->signalEof();
    if (videoStreamIndex_ >= 0) videoQueue_->signalEof();

    Logger::instance().info("DemuxWorker: finished, queues signaled");
    emit finished();
}

void DemuxWorker::abort() {
    aborted_ = true;
}
