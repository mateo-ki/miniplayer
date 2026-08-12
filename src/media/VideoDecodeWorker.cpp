#include "media/VideoDecodeWorker.h"

#include "infrastructure/Logger.h"
#include "media/PacketQueue.h"
#include "media/FrameQueue.h"
#include "media/VideoDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

void VideoDecodeWorker::configure(PacketQueue *queue, VideoDecoder *decoder, FrameQueue *frameQueue) {
    queue_ = queue;
    decoder_ = decoder;
    frameQueue_ = frameQueue;
}

void VideoDecodeWorker::setSeekTarget(double sec) {
    seekTargetSec_.store(sec);
}

void VideoDecodeWorker::pushFrame(QImage image) {
    if (image.isNull()) return;
    double ptsSec = image.text("pts").toDouble();
    double seekTarget = seekTargetSec_.load();
    if (seekTarget >= 0 && ptsSec >= 0 && ptsSec < seekTarget) {
        return;
    }
    if (seekTarget >= 0) {
        seekTargetSec_ = -1.0;
    }
    TimedVideoFrame frame;
    frame.image = std::move(image);
    frame.ptsSec = ptsSec;
    frameQueue_->push(std::move(frame));
}

void VideoDecodeWorker::start() {
    Logger::instance().info("VideoDecodeWorker started");
    int frameCount = 0;

    while (!aborted_) {
        AVPacket *pkt = queue_->pop();
        if (!pkt) {
            Logger::instance().info("VideoDecodeWorker: queue empty, "
                + QString::number(frameCount) + " frames produced");
            break;
        }

        QImage image = decoder_->decode(pkt);
        av_packet_free(&pkt);

        if (!image.isNull()) {
            pushFrame(std::move(image));
            ++frameCount;
        } else if (!aborted_) {
            Logger::instance().warn("VideoDecodeWorker: decode returned null frame, skipping");
        }
        // Drain any buffered frames from B-frame reordering
        while (true) {
            QImage buffered = decoder_->receiveBuffered();
            if (buffered.isNull()) break;
            pushFrame(std::move(buffered));
            ++frameCount;
        }
    }

    // Drain decoder to get remaining buffered frames
    if (!aborted_) {
        while (true) {
            QImage image = decoder_->decode(nullptr);
            if (image.isNull()) break;
            pushFrame(std::move(image));
            ++frameCount;
        }
        while (true) {
            QImage buffered = decoder_->receiveBuffered();
            if (buffered.isNull()) break;
            pushFrame(std::move(buffered));
            ++frameCount;
        }
        Logger::instance().info("VideoDecodeWorker: drain complete, "
            + QString::number(frameCount) + " total frames");
    }

    Logger::instance().info("VideoDecodeWorker: stopped");
    emit finished();
}

void VideoDecodeWorker::abort() {
    aborted_ = true;
    if (queue_) queue_->abort();
}
