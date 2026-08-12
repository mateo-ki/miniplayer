#pragma once

#include <atomic>

#include <QObject>
#include <QImage>

class PacketQueue;
class VideoDecoder;
class FrameQueue;

class VideoDecodeWorker final : public QObject {
    Q_OBJECT
public:
    void configure(PacketQueue *queue, VideoDecoder *decoder, FrameQueue *frameQueue);
    void setSeekTarget(double sec);

public slots:
    void start();
    void abort();

signals:
    void finished();

private:
    PacketQueue *queue_ = nullptr;
    VideoDecoder *decoder_ = nullptr;
    FrameQueue *frameQueue_ = nullptr;
    std::atomic<bool> aborted_ = false;
    std::atomic<double> seekTargetSec_{-1.0};

    void pushFrame(QImage image);
};
