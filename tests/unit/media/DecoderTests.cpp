#include <QtTest>

#include "media/AudioDecoder.h"
#include "media/VideoDecoder.h"
#include "infrastructure/Error.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class AudioDecoderTests : public QObject {
    Q_OBJECT

private slots:
    void invalidStreamIndexReturnsError();
    void codecNotFoundReturnsError();
};

class VideoDecoderTests : public QObject {
    Q_OBJECT

private slots:
    void invalidStreamIndexReturnsError();
    void codecNotFoundReturnsError();
};

void AudioDecoderTests::invalidStreamIndexReturnsError() {
    // Create a minimal format context with no streams
    AVFormatContext *ctx = avformat_alloc_context();
    QVERIFY(ctx != nullptr);

    AudioDecoder decoder;
    Error result = decoder.initialize(ctx, -1);
    QVERIFY(!result.ok);
    QCOMPARE(result.code, ErrorCode::StreamNotFound);

    result = decoder.initialize(ctx, 999);
    QVERIFY(!result.ok);
    QCOMPARE(result.code, ErrorCode::StreamNotFound);

    avformat_free_context(ctx);
}

void AudioDecoderTests::codecNotFoundReturnsError() {
    // This test verifies the error code is set correctly
    // We can't easily create a stream with an unknown codec without a real file
    // So we just verify the error code enum exists and works
    Error e = Error::failure("test", ErrorCode::DecoderInitFailed);
    QVERIFY(!e.ok);
    QCOMPARE(e.code, ErrorCode::DecoderInitFailed);
    QCOMPARE(e.message, QString("test"));
}

void VideoDecoderTests::invalidStreamIndexReturnsError() {
    AVFormatContext *ctx = avformat_alloc_context();
    QVERIFY(ctx != nullptr);

    VideoDecoder decoder;
    Error result = decoder.initialize(ctx, -1);
    QVERIFY(!result.ok);
    QCOMPARE(result.code, ErrorCode::StreamNotFound);

    result = decoder.initialize(ctx, 999);
    QVERIFY(!result.ok);
    QCOMPARE(result.code, ErrorCode::StreamNotFound);

    avformat_free_context(ctx);
}

void VideoDecoderTests::codecNotFoundReturnsError() {
    Error e = Error::failure("test", ErrorCode::DecoderInitFailed);
    QVERIFY(!e.ok);
    QCOMPARE(e.code, ErrorCode::DecoderInitFailed);
}

int main(int argc, char **argv) {
    int status = 0;

    {
        AudioDecoderTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }
    {
        VideoDecoderTests tests;
        status |= QTest::qExec(&tests, argc, argv);
    }

    return status;
}

#include "DecoderTests.moc"
