#pragma once

#include <atomic>
#include <vector>

#include <QObject>
#include <QImage>

#include "media/ImageHlsSegment.h"

class FrameQueue;
class PcmAudioPlayer;
class QNetworkReply;

class ImageHlsWorker final : public QObject {
    Q_OBJECT
public:
    void configure(std::vector<ImageHlsSegment> segments, FrameQueue *frameQueue,
                   int startSegment = 0, PcmAudioPlayer *audioPlayer = nullptr);

public slots:
    void start();
    void abort();

signals:
    void finished();
    void error(const QString &message);

private:
    std::vector<ImageHlsSegment> segments_;
    FrameQueue *frameQueue_ = nullptr;
    PcmAudioPlayer *audioPlayer_ = nullptr;
    int startSegment_ = 0;
    std::atomic<bool> aborted_ = false;
    QNetworkReply *currentReply_ = nullptr;
};
