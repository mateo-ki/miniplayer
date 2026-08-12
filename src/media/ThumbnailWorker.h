#pragma once

#include <QObject>
#include <QImage>

struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct AVStream;

class ThumbnailWorker final : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailWorker(QObject *parent = nullptr);

    void setSource(const QString &path);
    void request(double positionSec);

public slots:
    void start();
    void abort();

signals:
    void thumbnailReady(double positionSec, const QImage &image);
    void finished();

private:
    QString path_;
    double requestedPos_ = -1.0;
    bool aborted_ = false;

    AVFormatContext *fmtCtx_ = nullptr;
    AVCodecContext *codecCtx_ = nullptr;
    SwsContext *swsCtx_ = nullptr;
    AVStream *stream_ = nullptr;
    int videoStreamIndex_ = -1;

    bool openDecoder();
    void closeDecoder();
    QImage decodeAt(double positionSec);
};
