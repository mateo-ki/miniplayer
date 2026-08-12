#pragma once

#include <atomic>

#include <QObject>

struct AVFormatContext;
class PacketQueue;

class DemuxWorker final : public QObject {
    Q_OBJECT
public:
    void configure(AVFormatContext *ctx, PacketQueue *audioQueue,
                   PacketQueue *videoQueue, PacketQueue *subtitleQueue,
                   int audioStreamIndex, int videoStreamIndex, int subtitleStreamIndex);

public slots:
    void start();
    void abort();

signals:
    void finished();
    void error(const QString &message);

private:
    AVFormatContext *ctx_ = nullptr;
    PacketQueue *audioQueue_ = nullptr;
    PacketQueue *videoQueue_ = nullptr;
    PacketQueue *subtitleQueue_ = nullptr;
    int audioStreamIndex_ = -1;
    int videoStreamIndex_ = -1;
    int subtitleStreamIndex_ = -1;
    std::atomic<bool> aborted_ = false;
};
